use std::sync::Arc;

use ain_dftx::loans::SetLoanToken;
use defichain_rpc::json::blockchain::{Block, Transaction};
use rust_decimal::{prelude::Zero, Decimal};

use crate::{
    indexer::{Context, Index, Result},
    model::{
        OraclePriceActive, OraclePriceActiveActive, OraclePriceActiveActiveOracles,
        OraclePriceActiveNext, OraclePriceActiveNextOracles, OraclePriceAggregated,
    },
    repository::RepositoryOps,
    storage::SortOrder,
    Services,
};
impl Index for SetLoanToken {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let ticker_id = (self.currency_pair.token, self.currency_pair.currency);
        let aggregated_price = services
            .oracle_price_aggregated
            .by_key
            .list(Some(ticker_id.clone()), SortOrder::Descending)?
            .map(|item| {
                let (_, id) = item?;
                let b = services
                    .oracle_price_aggregated
                    .by_id
                    .get(&id)?
                    .ok_or("Missing oracle previous history index")?;

                Ok(b)
            })
            .collect::<std::result::Result<Vec<_>, Box<dyn std::error::Error>>>()?;

        if !aggregated_price.is_empty() {
            let previous_price = services
                .oracle_price_active
                .by_key
                .list(Some(ticker_id.clone()), SortOrder::Descending)?
                .map(|item| {
                    let (_, id) = item?;
                    let b = services
                        .oracle_price_active
                        .by_id
                        .get(&id)?
                        .ok_or("Missing oracle previous history index")?;

                    Ok(b)
                })
                .collect::<std::result::Result<Vec<_>, Box<dyn std::error::Error>>>()?;
            let price_active_id = (
                ticker_id.0.clone(),
                ticker_id.1.clone(),
                aggregated_price[0].block.height,
            );

            let oracle_price_key = (ticker_id.0, ticker_id.1);
            let next_price = match aggregated_validate(aggregated_price[0].clone(), ctx) {
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
                sort: hex::encode(ctx.block.height.to_be_bytes()),
                active: active_price.clone(),
                next: next_price.clone(),
                is_live: is_live(Some(active_price), Some(next_price)),
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
        }

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
pub fn aggregated_validate(aggrigated_price: OraclePriceAggregated, context: &Context) -> bool {
    let minimum_live_oracles = 2;
    if (aggrigated_price.block.time - context.block.time).abs() >= 3600 {
        return false;
    }
    if aggrigated_price.aggregated.oracles.active < minimum_live_oracles {
        return false;
    }

    if aggrigated_price.aggregated.weightage <= 0 {
        return false;
    }

    true
}

pub fn is_live(
    active: Option<OraclePriceActiveActive>,
    next: Option<OraclePriceActiveNext>,
) -> bool {
    if let (Some(active), Some(next)) = (active, next) {
        let active_price = match Decimal::from_str_exact(&active.amount) {
            Ok(num) => num,
            Err(_) => return false,
        };

        let next_price = match Decimal::from_str_exact(&next.amount) {
            Ok(num) => num,
            Err(_) => return false,
        };

        if active_price <= Decimal::zero() || next_price <= Decimal::zero() {
            return false;
        }

        let deviation_threshold = Decimal::new(5, 1); // This represents 0.5

        let diff = next_price - active_price;
        let abs_diff = diff.abs();
        let threshold = active_price * deviation_threshold;
        if abs_diff >= threshold {
            return false;
        }
        true
    } else {
        false
    }
}
pub fn invalidate_block_end(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
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
        let id_with_height = (ticker.id.0.clone(), ticker.id.1.clone(), block.height);
        services.oracle_price_active.by_id.delete(&id_with_height)?;
    }

    Ok(())
}
