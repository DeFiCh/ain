use std::sync::Arc;

use dftx_rs::pool::*;
use log::debug;

use super::Context;
use crate::{
    indexer::{tx_result, Index, Result},
    model::{self, PoolSwapResult, TxResult},
    repository::RepositoryOps,
    Services,
};

impl Index for PoolSwap {
    fn index(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[Poolswap] Indexing...");
        let txid = ctx.tx.txid;
        let idx = ctx.tx_idx;
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) =
            services.result.get(&txid)?
        else {
            debug!("Missing swap result for {}", ctx.tx.txid.to_string());
            return Err("Missing swap result".into());
        };

        let swap = model::PoolSwap {
            id: format!("{}-{}", pool_id, txid),
            sort: format!("{}-{}", ctx.block.height, idx),
            txid: txid,
            txno: idx,
            from_amount: self.from_amount,
            from_token_id: self.from_token_id.0,
            to_token_id: self.to_token_id.0,
            to_amount,
            pool_id,
            from: self.from_script.clone(),
            to: self.to_script.clone(),
            block: ctx.block.clone(),
        };
        debug!("swap : {:?}", swap);

        services
            .pool
            .by_id
            .put(&(pool_id, ctx.block.height, idx), &swap)
    }

    fn invalidate(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        let txid = ctx.tx.txid;
        let Some(TxResult::PoolSwap(PoolSwapResult { pool_id, .. })) =
            services.result.get(&txid)?
        else {
            return Err("Missing swap result".into());
        };

        services
            .pool
            .by_id
            .delete(&(pool_id, ctx.block.height, ctx.tx_idx))?;
        tx_result::invalidate(services.clone(), &txid)
    }
}

impl Index for CompositeSwap {
    fn index(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[CompositeSwap] Indexing...");
        let txid = ctx.tx.txid;
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, .. })) =
            services.result.get(&txid)?
        else {
            debug!("Missing swap result for {}", txid.to_string());
            return Err("Missing swap result".into());
        };

        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            let swap = model::PoolSwap {
                id: format!("{}-{}", pool_id, txid),
                sort: format!("{}-{}", ctx.block.height, ctx.tx_idx),
                txid: txid,
                txno: ctx.tx_idx,
                from_amount: self.pool_swap.from_amount,
                from_token_id: self.pool_swap.from_token_id.0,
                to_token_id: self.pool_swap.to_token_id.0,
                to_amount,
                pool_id,
                from: self.pool_swap.from_script.clone(),
                to: self.pool_swap.to_script.clone(),
                block: ctx.block.clone(),
            };
            debug!("swap : {:?}", swap);
            services
                .pool
                .by_id
                .put(&(pool_id, ctx.block.height, ctx.tx_idx), &swap)?;
        }

        Ok(())
    }

    fn invalidate(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            services
                .pool
                .by_id
                .delete(&(pool_id, ctx.block.height, ctx.tx_idx))?;
        }
        tx_result::invalidate(services.clone(), &ctx.tx.txid)
    }
}
