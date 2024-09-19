use std::{str::FromStr, sync::Arc};

use ain_dftx::{pool::*, COIN};
use bitcoin::Txid;
use log::trace;
use rust_decimal::Decimal;
use rust_decimal_macros::dec;
use snafu::OptionExt;

use super::Context;
use crate::{
    error::{ArithmeticOverflowSnafu, ArithmeticUnderflowSnafu},
    indexer::{tx_result, Index, Result},
    model::{self, PoolSwapResult, TxResult},
    storage::{RepositoryOps, SortOrder},
    Services,
};

pub const AGGREGATED_INTERVALS: [u32; 2] = [
    PoolSwapAggregatedInterval::OneDay as u32,
    PoolSwapAggregatedInterval::OneHour as u32,
];

#[derive(Debug, Clone, Copy)]
pub enum PoolSwapAggregatedInterval {
    OneDay = 60 * 60 * 24,
    OneHour = 60 * 60,
}

fn index_swap_aggregated(
    services: &Arc<Services>,
    pool_id: u32,
    from_token_id: u64,
    from_amount: i64,
    txid: Txid,
) -> Result<()> {
    for interval in AGGREGATED_INTERVALS {
        let repo: &crate::PoolSwapAggregatedService = &services.pool_swap_aggregated;
        let prevs = repo
            .by_key
            .list(Some((pool_id, interval, i64::MAX)), SortOrder::Descending)?
            .take(1)
            .take_while(|item| match item {
                Ok((k, _)) => k.0 == pool_id && k.1 == interval,
                _ => true,
            })
            .flatten()
            .collect::<Vec<_>>();

        if prevs.is_empty() {
            log::error!(
                "index swap {txid}: Unable to find {pool_id}-{interval} for Aggregate Indexing"
            );
            continue;
        }

        let Some((_, id)) = prevs.first() else {
            continue;
        };

        let aggregated = repo.by_id.get(id)?;

        let Some(mut aggregated) = aggregated else {
            continue;
        };

        let amount = aggregated
            .aggregated
            .amounts
            .get(&from_token_id)
            .map(|amt| Decimal::from_str(amt))
            .transpose()?
            .unwrap_or(dec!(0));

        let aggregated_amount = amount
            .checked_add(Decimal::from(from_amount) / Decimal::from(COIN))
            .context(ArithmeticOverflowSnafu)?;

        aggregated
            .aggregated
            .amounts
            .insert(from_token_id, format!("{aggregated_amount:.8}"));

        repo.by_id.put(id, &aggregated)?;
    }

    Ok(())
}

fn invalidate_swap_aggregated(
    services: &Arc<Services>,
    pool_id: u32,
    from_token_id: u64,
    from_amount: i64,
    txid: Txid,
) -> Result<()> {
    for interval in AGGREGATED_INTERVALS {
        let repo = &services.pool_swap_aggregated;
        let prevs = repo
            .by_key
            .list(Some((pool_id, interval, i64::MAX)), SortOrder::Descending)?
            .take(1)
            .take_while(|item| match item {
                Ok((k, _)) => k.0 == pool_id && k.1 == interval,
                _ => true,
            })
            .flatten()
            .collect::<Vec<_>>();

        if prevs.is_empty() {
            log::error!(
                "invalidate swap {txid}: Unable to find {pool_id}-{interval} for Aggregate Indexing"
            );
            continue;
        }

        let Some((_, id)) = prevs.first() else {
            continue;
        };

        let aggregated = repo.by_id.get(id)?;

        let Some(mut aggregated) = aggregated else {
            continue;
        };

        let amount = aggregated
            .aggregated
            .amounts
            .get(&from_token_id)
            .map(|amt| Decimal::from_str(amt))
            .transpose()?
            .unwrap_or(dec!(0));

        let aggregated_amount = amount
            .checked_sub(Decimal::from(from_amount) / Decimal::from(COIN))
            .context(ArithmeticUnderflowSnafu)?;

        aggregated
            .aggregated
            .amounts
            .insert(from_token_id, format!("{aggregated_amount:.8}"));

        repo.by_id.put(id, &aggregated)?;
    }

    Ok(())
}

impl Index for PoolSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        trace!("[Poolswap] Indexing...");
        let txid = ctx.tx.txid;
        let idx = ctx.tx_idx;
        let from = self.from_script;
        let to = self.to_script;
        let from_token_id = self.from_token_id.0;
        let from_amount = self.from_amount;
        let to_token_id = self.to_token_id.0;

        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) =
            services.result.get(&txid)?
        else {
            // TODO: Commenting out for now, fallback should only be introduced for supporting back CLI indexing
            return Err("Missing swap result".into());
            // let pair = find_pair(from_token_id, to_token_id);
            // if pair.is_none() {
            //     return Err(format_err!("Pool not found by {from_token_id}-{to_token_id} or {to_token_id}-{from_token_id}").into());
            // }
            // let pair = pair.unwrap();
            // (None, pair.id)
        };

        let swap: model::PoolSwap = model::PoolSwap {
            txid,
            txno: idx,
            from_amount,
            from_token_id,
            to_token_id,
            to_amount,
            pool_id,
            from,
            to,
            block: ctx.block.clone(),
        };
        trace!("swap : {:?}", swap);

        services
            .pool
            .by_id
            .put(&(pool_id, ctx.block.height, idx), &swap)?;

        index_swap_aggregated(services, pool_id, from_token_id, from_amount, txid)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        trace!("[PoolSwap] Invalidating...");
        let txid = ctx.tx.txid;
        let from_token_id = self.from_token_id.0;
        let from_amount = self.from_amount;

        let Some(TxResult::PoolSwap(PoolSwapResult { pool_id, .. })) =
            services.result.get(&txid)?
        else {
            return Err("Missing swap result".into());
        };

        services
            .pool
            .by_id
            .delete(&(pool_id, ctx.block.height, ctx.tx_idx))?;
        tx_result::invalidate(services, &txid)?;

        invalidate_swap_aggregated(services, pool_id, from_token_id, from_amount, txid)?;

        Ok(())
    }
}

impl Index for CompositeSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        trace!("[CompositeSwap] Indexing...");
        let txid = ctx.tx.txid;
        let from_token_id = self.pool_swap.from_token_id.0;
        let from_amount = self.pool_swap.from_amount;
        let to_token_id = self.pool_swap.to_token_id.0;

        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) =
            services.result.get(&txid)?
        else {
            trace!("Missing swap result for {}", txid.to_string());
            return Err("Missing swap result".into());
        };

        let from = self.pool_swap.from_script;
        let to = self.pool_swap.to_script;
        let pools = self.pools.as_ref();

        let pool_ids = if pools.is_empty() {
            // the pool_id from finals wap is the only swap while pools is empty
            Vec::from([pool_id])
        } else {
            pools.iter().map(|pool| pool.id.0 as u32).collect()
        };

        for pool_id in pool_ids {
            let swap = model::PoolSwap {
                txid,
                txno: ctx.tx_idx,
                from_amount: self.pool_swap.from_amount,
                from_token_id,
                to_token_id,
                to_amount,
                pool_id,
                from: from.clone(),
                to: to.clone(),
                block: ctx.block.clone(),
            };
            services
                .pool
                .by_id
                .put(&(pool_id, ctx.block.height, ctx.tx_idx), &swap)?;

            index_swap_aggregated(services, pool_id, from_token_id, from_amount, txid)?;
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        trace!("[ComposoteSwap] Invalidating...");
        let from_token_id = self.pool_swap.from_token_id.0;
        let from_amount = self.pool_swap.from_amount;
        let txid = ctx.tx.txid;
        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            services
                .pool
                .by_id
                .delete(&(pool_id, ctx.block.height, ctx.tx_idx))?;

            invalidate_swap_aggregated(services, pool_id, from_token_id, from_amount, txid)?;
        }
        tx_result::invalidate(services, &ctx.tx.txid)
    }
}
