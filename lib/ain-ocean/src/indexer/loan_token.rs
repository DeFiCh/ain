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
        let ticker = (self.currency_pair.token, self.currency_pair.currency);
        perform_active_price_tick(services, ticker, &ctx.block)?;
        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        invalidate_transaction(
            services,
            context.block.height,
            (
                self.currency_pair.token.clone(),
                self.currency_pair.currency.clone(),
            ),
        )?;
        Ok(())
    }
}

fn is_aggregate_valid(aggregate: &OraclePriceAggregated, block: &BlockContext) -> bool {
    if (aggregate.block.time - block.time).abs() >= 3600 {
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
    let Some(active) = active else {
        return false;
    };

    let Some(next) = next else {
        return false;
    };

    let Ok(active_price) = Decimal::from_str(&active.amount) else {
        return false;
    };

    let Ok(next_price) = Decimal::from_str(&next.amount) else {
        return false;
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

pub fn index_active_price(services: &Arc<Services>, block: &BlockContext) -> Result<()> {
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
            perform_active_price_tick(services, ticker.id, block)?;
        }
    }
    Ok(())
}

pub fn perform_active_price_tick(
    services: &Arc<Services>,
    ticker_id: (String, String),
    block: &BlockContext,
) -> Result<()> {
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
                .ok_or("aggregated price cannot be found")?;

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

    let aggregated_price = if let Some(price) = aggregated_prices.first() {
        price
    } else {
        return Ok(());
    };

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
                .ok_or("price_active id does not exist")?;
            Ok(price)
        })
        .collect::<Result<Vec<_>>>()?;

    let active_price = if let Some(price) = previous_prices.first().and_then(|p| p.next.clone()) {
        Some(OraclePriceActiveActive {
            amount: price.amount,
            weightage: price.weightage,
            oracles: OraclePriceActiveActiveOracles {
                active: price.oracles.active,
                total: price.oracles.total,
            },
        })
    } else if let Some(price) = previous_prices.first().and_then(|p| p.active.clone()) {
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
    };

    let price_active_id = (
        ticker_id.0.clone(),
        ticker_id.1.clone(),
        aggregated_price.block.height,
    );

    let next_price = if is_aggregate_valid(aggregated_price, block) {
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
        sort: hex::encode(block.height.to_be_bytes()),
        active: active_price.clone(),
        next: next_price.clone(),
        is_live: is_live(active_price, next_price),
        block: block.clone(),
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
pub fn invalidate_block_end(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    let block_interval = match Network::Regtest {
        Network::Regtest => 6,
        _ => 120,
    };

    if block.height % block_interval != 0 {
        return Ok(());
    }
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
pub fn invalidate_transaction(
    services: &Arc<Services>,
    block_height: u32,
    ticker: (String, String),
) -> Result<()> {
    let ticker_id = (ticker.0, ticker.1, block_height);
    services.oracle_price_active.by_id.delete(&ticker_id)?;
    services
        .oracle_price_active
        .by_key
        .delete(&(ticker_id.0, ticker_id.1))?;
    Ok(())
}
