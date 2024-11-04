use std::{str::FromStr, sync::Arc};

use ain_cpp_imports::PoolPairCreationHeight;
use ain_dftx::{pool::*, COIN};
use bitcoin::Txid;
use log::trace;
use parking_lot::RwLock;
use rust_decimal::Decimal;
use rust_decimal_macros::dec;
use snafu::OptionExt;

use super::{Context, IndexBlockStart};
use crate::{
    error::{ArithmeticOverflowSnafu, ArithmeticUnderflowSnafu, Error, NotFoundKind},
    indexer::{tx_result, Index, Result},
    model::{
        self, BlockContext, PoolSwapAggregated, PoolSwapAggregatedAggregated, PoolSwapResult,
        TxResult,
    },
    storage::{RepositoryOps, SortOrder},
    PoolSwapAggregatedService, Services,
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

fn create_new_bucket(
    repo: &PoolSwapAggregatedService,
    bucket: i64,
    pool_pair_id: u32,
    interval: u32,
    block: &BlockContext,
) -> Result<()> {
    let aggregated = PoolSwapAggregated {
        bucket,
        aggregated: PoolSwapAggregatedAggregated {
            amounts: Default::default(),
        },
        block: BlockContext {
            hash: block.hash,
            height: block.height,
            time: block.time,
            median_time: block.median_time,
        },
    };

    let pool_swap_aggregated_key = (pool_pair_id, interval, bucket);
    let pool_swap_aggregated_id = (pool_pair_id, interval, block.hash);

    repo.by_key
        .put(&pool_swap_aggregated_key, &pool_swap_aggregated_id)?;
    repo.by_id.put(&pool_swap_aggregated_id, &aggregated)?;

    Ok(())
}

impl IndexBlockStart for PoolSwap {
    fn index_block_start(self, services: &Arc<Services>, block: &BlockContext) -> Result<()> {
        let mut pool_pairs =  ain_cpp_imports::get_pool_pairs();
        pool_pairs.sort_by(|a, b| b.creation_height.cmp(&a.creation_height));

        for interval in AGGREGATED_INTERVALS {
            for pool_pair in &pool_pairs {
                let repo = &services.pool_swap_aggregated;

                let prev = repo
                    .by_key
                    .list(
                        Some((pool_pair.id, interval, i64::MAX)),
                        SortOrder::Descending,
                    )?
                    .take_while(|item| match item {
                        Ok((k, _)) => k.0 == pool_pair.id && k.1 == interval,
                        _ => true,
                    })
                    .next()
                    .transpose()?;

                let bucket = block.median_time - (block.median_time % interval as i64);

                let Some((_, prev_id)) = prev else {
                    create_new_bucket(repo, bucket, pool_pair.id, interval, block)?;
                    continue;
                };

                let Some(prev) = repo.by_id.get(&prev_id)? else {
                    create_new_bucket(repo, bucket, pool_pair.id, interval, block)?;
                    continue;
                };

                if prev.bucket >= bucket {
                    break;
                }

                create_new_bucket(repo, bucket, pool_pair.id, interval, block)?;
            }
        }

        Ok(())
    }

    fn invalidate_block_start(self, services: &Arc<Services>, block: &BlockContext) -> Result<()> {
        let mut pool_pairs = ain_cpp_imports::get_pool_pairs();
        pool_pairs.sort_by(|a, b| b.creation_height.cmp(&a.creation_height));

        for interval in AGGREGATED_INTERVALS {
            for pool_pair in &pool_pairs {
                let pool_swap_aggregated_id = (pool_pair.id, interval, block.hash);
                services
                    .pool_swap_aggregated
                    .by_id
                    .delete(&pool_swap_aggregated_id)?;
            }
        }

        Ok(())
    }
}

impl Index for PoolSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        trace!("[Poolswap] Indexing {self:?}...");
        let txid = ctx.tx.txid;
        let idx = ctx.tx_idx;
        let from = self.from_script;
        let to = self.to_script;
        let from_token_id = self.from_token_id.0;
        let from_amount = self.from_amount;
        let to_token_id = self.to_token_id.0;

        let (to_amount, pool_id) = match services.result.get(&txid)? {
            Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) => {
                (Some(to_amount), pool_id)
            }
            _ => {
                let poolpairs = services.pool_pair_cache.get();

                let pool_id = poolpairs
                    .into_iter()
                    .find(|pp| {
                        (pp.id_token_a == self.from_token_id.0 as u32
                            && pp.id_token_b == self.to_token_id.0 as u32)
                            || (pp.id_token_a == self.to_token_id.0 as u32
                                && pp.id_token_b == self.from_token_id.0 as u32)
                    })
                    .map(|pp| pp.id)
                    .ok_or(Error::NotFound {
                        kind: NotFoundKind::PoolPair,
                    })?;

                (None, pool_id)
            }
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
        trace!("[CompositeSwap] Indexing {self:?}...");
        let txid = ctx.tx.txid;
        let from_token_id = self.pool_swap.from_token_id.0;
        let from_amount = self.pool_swap.from_amount;
        let to_token_id = self.pool_swap.to_token_id.0;

        let (to_amount, pool_id) = match services.result.get(&txid)? {
            Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) => {
                (Some(to_amount), Some(pool_id))
            }
            _ => {
                let poolpairs = services.pool_pair_cache.get();

                let pool_id = poolpairs
                    .into_iter()
                    .find(|pp| {
                        (pp.id_token_a == self.pool_swap.from_token_id.0 as u32
                            && pp.id_token_b == self.pool_swap.to_token_id.0 as u32)
                            || (pp.id_token_a == self.pool_swap.to_token_id.0 as u32
                                && pp.id_token_b == self.pool_swap.from_token_id.0 as u32)
                    })
                    .map(|pp| pp.id);

                (None, pool_id)
            }
        };

        let from = self.pool_swap.from_script;
        let to = self.pool_swap.to_script;
        let pools = self.pools.as_ref();

        let pool_ids = if pools.is_empty() {
            // the pool_id from finals wap is the only swap while pools is empty
            let pool_id = pool_id.ok_or(Error::NotFound {
                kind: NotFoundKind::PoolPair,
            })?;
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

#[derive(Default)]
pub struct PoolPairCache {
    cache: RwLock<Option<Vec<PoolPairCreationHeight>>>,
}

impl PoolPairCache {
    pub fn new() -> Self {
        Self {
            cache: RwLock::new(None),
        }
    }

    pub fn get(&self) -> Vec<PoolPairCreationHeight> {
        {
            let guard = self.cache.read();
            if let Some(poolpairs) = guard.as_ref() {
                return poolpairs.clone();
            }
        }

        let poolpairs = ain_cpp_imports::get_pool_pairs();

        let mut guard = self.cache.write();
        *guard = Some(poolpairs.clone());

        poolpairs
    }

    pub fn invalidate(&self) {
        let mut guard = self.cache.write();
        *guard = None;
    }
}
