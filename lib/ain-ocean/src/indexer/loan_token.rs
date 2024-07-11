use std::{str::FromStr, sync::Arc};

use ain_dftx::loans::SetLoanToken;
use defichain_rpc::json::blockchain::{Block, Transaction};
use rust_decimal::{prelude::Zero, Decimal};
use rust_decimal_macros::dec;

use crate::{
    indexer::{Context, Index, Result},
    model::{
        BlockContext, OraclePriceActive, OraclePriceActiveActive, OraclePriceActiveActiveOracles,
        OraclePriceActiveNext, OraclePriceActiveNextOracles, OraclePriceAggregated,
    },
    network::Network,
    repository::RepositoryOps,
    storage::SortOrder,
    Services,
};

impl Index for SetLoanToken {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let ticker_id = (self.currency_pair.token, self.currency_pair.currency);
        let aggregated_prices = services
            .oracle_price_aggregated
            .by_key
            .list(Some(ticker_id.clone()), SortOrder::Descending)?
            .map(|item| {
                let (_, id) = item?;
                let aggregated = services
                    .oracle_price_aggregated
                    .by_id
                    .get(&id)?
                    .ok_or("Missing oracle aggregated price index")?;

                Ok(aggregated)
            })
            .collect::<Result<Vec<_>>>()?;

        log::debug!(
            "set_loan_token indexing aggregated_price: {:?}",
            aggregated_prices
        );

        if aggregated_prices.is_empty() {
            return Ok(());
        }
        let aggregated_price = aggregated_prices.first().unwrap();

        let previous_prices = services
            .oracle_price_active
            .by_key
            .list(Some(ticker_id.clone()), SortOrder::Descending)?
            .take(1)
            .map(|item| {
                let (_, id) = item?;
                let price = services
                    .oracle_price_active
                    .by_id
                    .get(&id)?
                    .ok_or("Missing oracle previous history index")?;
                Ok(price)
            })
            .collect::<Result<Vec<_>>>()?;

        let active_price = if previous_prices.first().is_some() {
            if previous_prices[0].next.is_some() {
                let price = previous_prices[0].next.clone().unwrap();
                Some(OraclePriceActiveActive {
                    amount: price.amount,
                    weightage: price.weightage,
                    oracles: OraclePriceActiveActiveOracles {
                        active: price.oracles.active,
                        total: price.oracles.total,
                    },
                })
            } else if previous_prices[0].active.is_some() {
                let price = previous_prices[0].active.clone().unwrap();
                Some(OraclePriceActiveActive {
                    amount: price.amount,
                    weightage: price.weightage,
                    oracles: OraclePriceActiveActiveOracles {
                        active: price.oracles.active,
                        total: price.oracles.total,
                    },
                })
            } else {
                None
            }
        } else {
            None
        };

        let price_active_id = (
            ticker_id.0.clone(),
            ticker_id.1.clone(),
            aggregated_price.block.height,
        );

        let next_price = if is_aggregate_valid(aggregated_price, ctx) {
            Some(OraclePriceActiveNext {
                amount: aggregated_price.aggregated.amount.clone(),
                weightage: aggregated_price.aggregated.weightage,
                oracles: OraclePriceActiveNextOracles {
                    active: aggregated_price.aggregated.oracles.active,
                    total: aggregated_price.aggregated.oracles.total,
                },
            })
        } else {
            None
        };

        let oracle_price_active = OraclePriceActive {
            id: price_active_id.clone(),
            key: ticker_id,
            sort: hex::encode(ctx.block.height.to_be_bytes()),
            active: active_price.clone(),
            next: next_price.clone(),
            is_live: is_live(active_price, next_price),
            block: ctx.block.clone(),
        };

        services
            .oracle_price_active
            .by_id
            .put(&price_active_id, &oracle_price_active)?;

        services
            .oracle_price_active
            .by_key
            .put(&oracle_price_active.key, &oracle_price_active.id)?;

        log::debug!(
            "set_loan_token indexing oracle_price_active: {:?}",
            oracle_price_active
        );

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let ticker_id = (
            self.currency_pair.token.clone(),
            self.currency_pair.currency.clone(),
            context.block.height,
        );
        services.oracle_price_active.by_id.delete(&ticker_id)?;
        services
            .oracle_price_active
            .by_key
            .delete(&(ticker_id.0, ticker_id.1))?;
        Ok(())
    }
}

fn is_aggregate_valid(aggregate: &OraclePriceAggregated, context: &Context) -> bool {
    if (aggregate.block.time - context.block.time).abs() >= 3600 {
        return false;
    }

    if aggregate.aggregated.oracles.active < 2 {
        // minimum live oracles
        return false;
    }

    if aggregate.aggregated.weightage <= 0 {
        return false;
    }

    true
}

fn is_live(active: Option<OraclePriceActiveActive>, next: Option<OraclePriceActiveNext>) -> bool {
    if active.is_none() {
        return false;
    }

    if next.is_none() {
        return false;
    }

    let active = active.unwrap();
    let next = next.unwrap();

    let active_price = match Decimal::from_str(&active.amount) {
        Ok(num) => num,
        Err(_) => return false,
    };

    let next_price = match Decimal::from_str(&next.amount) {
        Ok(num) => num,
        Err(_) => return false,
    };

    if active_price <= Decimal::zero() {
        return false;
    }

    if next_price <= Decimal::zero() {
        return false;
    }

    let diff = (next_price - active_price).abs();
    let threshold = active_price * dec!(0.3); // deviation_threshold 0.3
    if diff >= threshold {
        return false;
    }

    true
}

pub fn index_block_end(services: &Arc<Services>, block: &Block<Transaction>) -> Result<()> {
    let block_interval = match Network::Regtest {
        Network::Regtest => 6,
        _ => 120,
    };
    if block.height % block_interval == 0 {
        let pt = services
            .price_ticker
            .by_id
            .list(None, SortOrder::Ascending)?
            .map(|item| {
                let (_, priceticker) = item?;
                Ok(priceticker)
            })
            .collect::<Result<Vec<_>>>()?;

        for ticker in pt {
            let aggregated_price = services
                .oracle_price_aggregated
                .by_key
                .list(Some(ticker.id.clone()), SortOrder::Descending)?
                .map(|item| {
                    let (_, id) = item?;
                    let b = services
                        .oracle_price_aggregated
                        .by_id
                        .get(&id)?
                        .ok_or("Missing oracle aggregated_price index")?;

                    Ok(b)
                })
                .collect::<std::result::Result<Vec<_>, Box<dyn std::error::Error>>>()?;

            if !aggregated_price.is_empty() {
                let previous_price = services
                    .oracle_price_active
                    .by_key
                    .list(Some(ticker.id.clone()), SortOrder::Descending)?
                    .map(|item| {
                        let (_, id) = item?;
                        let b = services
                            .oracle_price_active
                            .by_id
                            .get(&id)?
                            .ok_or("Missing oracle price active index")?;

                        Ok(b)
                    })
                    .collect::<std::result::Result<Vec<_>, Box<dyn std::error::Error>>>()?;
                let price_active_id = (
                    ticker.id.0.clone(),
                    ticker.id.1.clone(),
                    aggregated_price[0].block.height,
                );

                let oracle_price_key = (ticker.id.0.clone(), ticker.id.1.clone());
                let next_price = match aggregated_validate(aggregated_price[0].clone(), block.time)
                {
                    true => OraclePriceActiveNext {
                        amount: aggregated_price[0].aggregated.amount.clone(),
                        weightage: aggregated_price[0].aggregated.weightage,
                        oracles: OraclePriceActiveNextOracles {
                            active: aggregated_price[0].aggregated.oracles.active,
                            total: aggregated_price[0].aggregated.oracles.total,
                        },
                    },
                    false => Default::default(),
                };

                let active_price: OraclePriceActiveActive;

                if previous_price.is_empty() {
                    active_price = OraclePriceActiveActive {
                        amount: Default::default(),
                        weightage: Default::default(),
                        oracles: OraclePriceActiveActiveOracles {
                            active: Default::default(),
                            total: Default::default(),
                        },
                    };
                } else if let Some(next) = previous_price.first().map(|price| &price.next) {
                    active_price = OraclePriceActiveActive {
                        amount: next.amount.clone(),
                        weightage: next.weightage,
                        oracles: OraclePriceActiveActiveOracles {
                            active: next.oracles.active,
                            total: next.oracles.total,
                        },
                    };
                } else {
                    let oracles = OraclePriceActiveActiveOracles {
                        active: previous_price[0].active.oracles.active,
                        total: previous_price[0].active.oracles.total,
                    };
                    active_price = OraclePriceActiveActive {
                        amount: previous_price[0].active.amount.clone(),
                        weightage: previous_price[0].active.weightage,
                        oracles,
                    };
                }

                let oracle_price_active = OraclePriceActive {
                    id: price_active_id.clone(),
                    key: oracle_price_key,
                    sort: hex::encode(block.height.to_be_bytes()),
                    active: active_price.clone(),
                    next: next_price.clone(),
                    is_live: is_live(Some(active_price), Some(next_price)),
                    block: BlockContext {
                        hash: block.hash,
                        height: block.height,
                        time: block.time,
                        median_time: block.mediantime,
                    },
                };
                services
                    .oracle_price_active
                    .by_id
                    .put(&price_active_id, &oracle_price_active)?;
                services
                    .oracle_price_active
                    .by_key
                    .put(&oracle_price_active.key, &oracle_price_active.id)?;
            }
        }
    }
    Ok(())
}
