use std::{str::FromStr, sync::Arc};

use ain_dftx::{loans::SetLoanToken, Currency, Token};
use log::trace;
use rust_decimal::{prelude::Zero, Decimal};
use rust_decimal_macros::dec;

use crate::{
    indexer::{Context, Index, Result},
    model::{BlockContext, OraclePriceActive, OraclePriceActiveNext, OraclePriceAggregated},
    network::Network,
    storage::{RepositoryOps, SortOrder},
    Services,
};

impl Index for SetLoanToken {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let ticker = (self.currency_pair.token, self.currency_pair.currency);
        perform_active_price_tick(services, ticker, &ctx.block)?;
        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        trace!("[SetLoanToken] Invalidating...");
        let ticker_id = (
            self.currency_pair.token.clone(),
            self.currency_pair.currency.clone(),
            context.block.height.to_be_bytes(),
        );
        services.oracle_price_active.by_id.delete(&ticker_id)?;
        Ok(())
    }
}

fn is_aggregate_valid(aggregate: &OraclePriceAggregated, block: &BlockContext) -> bool {
    if (aggregate.block.time - block.time).abs() >= 3600 {
        return false;
    }

    if aggregate.aggregated.oracles.active < dec!(2) {
        // minimum live oracles
        return false;
    }

    if aggregate.aggregated.weightage <= dec!(0) {
        return false;
    }

    true
}

fn is_live(active: Option<OraclePriceActiveNext>, next: Option<OraclePriceActiveNext>) -> bool {
    let Some(active) = active else {
        return false;
    };

    let Some(next) = next else {
        return false;
    };

    let active_price = active.amount;

    let next_price = next.amount;

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
    let network = ain_cpp_imports::get_network();
    let block_interval = match Network::from_str(&network)? {
        Network::Regtest => 6,
        _ => 120,
    };
    if block.height % block_interval == 0 {
        let price_tickers = services
            .price_ticker
            .by_id
            .list(None, SortOrder::Descending)?
            .flatten()
            .collect::<Vec<_>>();

        for (ticker_id, _) in price_tickers {
            perform_active_price_tick(services, ticker_id, block)?;
        }
    }
    Ok(())
}

fn map_active_price(
    block: &BlockContext,
    aggregated_price: OraclePriceAggregated,
    prev_price: Option<OraclePriceActive>,
) -> OraclePriceActive {
    let next_price = if is_aggregate_valid(&aggregated_price, block) {
        Some(aggregated_price.aggregated)
    } else {
        None
    };

    let active_price = if let Some(prev_price) = prev_price {
        if let Some(next) = prev_price.next {
            Some(next)
        } else {
            prev_price.active
        }
    } else {
        None
    };

    OraclePriceActive {
        active: active_price.clone(),
        next: next_price.clone(),
        is_live: is_live(active_price, next_price),
        block: block.clone(),
    }
}

pub fn invalidate_active_price(services: &Arc<Services>, block: &BlockContext) -> Result<()> {
    let network = ain_cpp_imports::get_network();
    let block_interval = match Network::from_str(&network)? {
        Network::Regtest => 6,
        _ => 120,
    };
    if block.height % block_interval == 0 {
        let price_tickers = services
            .price_ticker
            .by_id
            .list(None, SortOrder::Descending)?
            .flatten()
            .collect::<Vec<_>>();

        for ((token, currency), _) in price_tickers.into_iter().rev() {
            services.oracle_price_active.by_id.delete(&(
                token,
                currency,
                block.height.to_be_bytes(),
            ))?;
        }
    }

    Ok(())
}

pub fn perform_active_price_tick(
    services: &Arc<Services>,
    ticker_id: (Token, Currency),
    block: &BlockContext,
) -> Result<()> {
    let id = (
        ticker_id.0.clone(),
        ticker_id.1.clone(),
        [0xffu8; 8],
        [0xffu8; 4],
    );

    let prev = services
        .oracle_price_aggregated
        .by_id
        .list(Some(id.clone()), SortOrder::Descending)?
        .next()
        .transpose()?;

    let Some((_, aggregated_price)) = prev else {
        return Ok(());
    };

    let id = (ticker_id.0, ticker_id.1, [0xffu8; 4]);
    let repo = &services.oracle_price_active;
    let prev = repo
        .by_id
        .list(Some(id.clone()), SortOrder::Descending)?
        .next()
        .transpose()?;

    let prev_price = if let Some((_, prev)) = prev {
        Some(prev)
    } else {
        None
    };

    let active_price = map_active_price(block, aggregated_price, prev_price);

    repo.by_id
        .put(&(id.0, id.1, block.height.to_be_bytes()), &active_price)?;

    Ok(())
}
