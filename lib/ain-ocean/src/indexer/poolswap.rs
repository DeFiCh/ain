use std::{ops::Div, str::FromStr, sync::Arc};

use ain_dftx::pool::*;
use anyhow::format_err;
use bitcoin::{BlockHash, Txid};
// use bitcoin::Address;
use log::debug;
use rust_decimal::Decimal;
use rust_decimal_macros::dec;

use super::Context;
use crate::{
    indexer::{tx_result, Index, Result},
    model::{self, PoolSwapResult, TxResult},
    repository::{RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Error, Services,
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

#[derive(Debug, Clone)]
pub struct PoolCreationHeight {
    pub id: u32,
    pub id_token_a: u32,
    pub id_token_b: u32,
    pub creation_height: u32,
}

fn index_swap_aggregated(
    services: &Arc<Services>,
    pool_id: u32,
    from_token_id: u64,
    from_amount: i64,
    txid: Txid,
) -> Result<()> {
    for interval in AGGREGATED_INTERVALS {
        let repository = &services.pool_swap_aggregated;
        let mut prevs = repository
            .by_key
            .list(Some((pool_id, interval, i64::MAX)), SortOrder::Descending)?
            .take(1)
            .take_while(|item| match item {
                Ok((k, _)) => k.0 == pool_id && k.1 == interval,
                _ => true,
            })
            .map(|e| repository.by_key.retrieve_primary_value(e))
            .collect::<Result<Vec<_>>>()?;

        if prevs.is_empty() {
            log::error!(
                "index swap {txid}: Unable to find {pool_id}-{interval} for Aggregate Indexing"
            );
            continue;
        }

        let aggregated = prevs.first_mut();
        if let Some(aggregated) = aggregated {
            let amount = aggregated
                .aggregated
                .amounts
                .get(&from_token_id.to_string())
                .map(|amt| Decimal::from_str(amt))
                .transpose()?
                .unwrap_or(dec!(0));

            let aggregated_amount = amount
                .checked_add(Decimal::from(from_amount).div(dec!(100_000_000)))
                .ok_or(Error::OverflowError)?;

            aggregated.aggregated.amounts.insert(
                from_token_id.to_string(),
                format!("{:.8}", aggregated_amount),
            );

            let parts = aggregated.id.split('-').collect::<Vec<&str>>();
            if parts.len() != 3 {
                return Err(format_err!("Invalid poolswap aggregated id format").into());
            };
            let pool_id = parts[0].parse::<u32>()?;
            let interval = parts[1].parse::<u32>()?;
            let hash = parts[2].parse::<BlockHash>()?;

            repository
                .by_id
                .put(&(pool_id, interval, hash), aggregated)?;
        }
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
        let repository = &services.pool_swap_aggregated;
        let mut prevs = repository
            .by_key
            .list(Some((pool_id, interval, i64::MAX)), SortOrder::Descending)?
            .take(1)
            .take_while(|item| match item {
                Ok((k, _)) => k.0 == pool_id && k.1 == interval,
                _ => true,
            })
            .map(|e| repository.by_key.retrieve_primary_value(e))
            .collect::<Result<Vec<_>>>()?;

        if prevs.is_empty() {
            log::error!(
                "invalidate swap {txid}: Unable to find {pool_id}-{interval} for Aggregate Indexing"
            );
            continue;
        }

        let aggregated = prevs.first_mut();
        if let Some(aggregated) = aggregated {
            let amount = aggregated
                .aggregated
                .amounts
                .get(&from_token_id.to_string())
                .map(|amt| Decimal::from_str(amt))
                .transpose()?
                .unwrap_or(dec!(0));

            let aggregated_amount = amount
                .checked_sub(Decimal::from(from_amount).div(dec!(100_000_000)))
                .ok_or(Error::UnderflowError)?;

            aggregated.aggregated.amounts.insert(
                from_token_id.to_string(),
                format!("{:.8}", aggregated_amount),
            );

            let parts = aggregated.id.split('-').collect::<Vec<&str>>();
            if parts.len() != 3 {
                return Err(format_err!("Invalid poolswap aggregated id format").into());
            };
            let pool_id = parts[0].parse::<u32>()?;
            let interval = parts[1].parse::<u32>()?;
            let hash = parts[2].parse::<BlockHash>()?;

            repository
                .by_id
                .put(&(pool_id, interval, hash), aggregated)?;
        }
    }

    Ok(())
}

impl Index for PoolSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[Poolswap] Indexing...");
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

        let swap = model::PoolSwap {
            id: format!("{}-{}", pool_id, txid),
            // TODO: use hex::encode eg: sort: hex::encode(ctx.block.height + idx)
            sort: format!("{}-{}", ctx.block.height, idx),
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
        debug!("swap : {:?}", swap);

        services
            .pool
            .by_id
            .put(&(pool_id, ctx.block.height, idx), &swap)?;

        index_swap_aggregated(services, pool_id, from_token_id, from_amount, txid)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
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
        debug!("[CompositeSwap] Indexing...");
        let txid = ctx.tx.txid;
        let from_token_id = self.pool_swap.from_token_id.0;
        let from_amount = self.pool_swap.from_amount;
        let to_token_id = self.pool_swap.to_token_id.0;

        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, .. })) =
            services.result.get(&txid)?
        else {
            debug!("Missing swap result for {}", txid.to_string());
            return Err("Missing swap result".into());
        };

        let from = self.pool_swap.from_script;
        let to = self.pool_swap.to_script;
        let pools = self.pools.as_ref();

        let pool_ids = if pools.is_empty() {
            let Some(pool_id) = services
                .poolpair
                .by_id
                .get(&(from_token_id as u32, to_token_id as u32))?
            else {
                return Err("Missing pool_id".into());
            };
            Vec::from([pool_id])
        } else {
            pools.iter().map(|pool| pool.id.0 as u32).collect()
        };

        for pool_id in pool_ids {
            let swap = model::PoolSwap {
                id: format!("{}-{}", pool_id, txid),
                sort: format!("{}-{}", ctx.block.height, ctx.tx_idx),
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
