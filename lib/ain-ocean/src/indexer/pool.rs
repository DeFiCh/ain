use std::sync::Arc;

use ain_dftx::pool::*;
// use anyhow::format_err;
// use bitcoin::Address;
use log::debug;

use super::Context;
use crate::{
    indexer::{tx_result, Index, Result},
    model::{self, PoolSwapResult, TxResult},
    repository::RepositoryOps,
    Services,
};

impl Index for PoolSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[Poolswap] Indexing...");
        let txid = ctx.tx.txid;
        let idx = ctx.tx_idx;
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) =
            services.result.get(&txid)?
        else {
            // TODO fallback through getaccounthistory when indexing against non-oceanarchive node
            // let address = Address::from_script(&self.to_script, bitcoin::Network::Bitcoin)
            //     .map_err(|e| format_err!("Error getting address from script: {e}"));
            debug!("Missing swap result for {}", ctx.tx.txid.to_string());
            return Err("Missing swap result".into());
        };

        let from = self.from_script;
        let to = self.to_script;

        let swap = model::PoolSwap {
            id: format!("{}-{}", pool_id, txid),
            sort: format!("{}-{}", ctx.block.height, idx),
            txid,
            txno: idx,
            from_amount: self.from_amount,
            from_token_id: self.from_token_id.0,
            to_token_id: self.to_token_id.0,
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
            .put(&(pool_id, ctx.block.height, idx), &swap)
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
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
        tx_result::invalidate(services, &txid)
    }
}

impl Index for CompositeSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[CompositeSwap] Indexing...");
        let txid = ctx.tx.txid;
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, .. })) =
            services.result.get(&txid)?
        else {
            // TODO fallback through getaccounthistory when indexing against non-oceanarchive node
            // let address =
            //     Address::from_script(&self.pool_swap.to_script, bitcoin::Network::Bitcoin)
            //         .map_err(|e| format_err!("Error getting address from script: {e}"));
            debug!("Missing swap result for {}", txid.to_string());
            return Err("Missing swap result".into());
        };

        let from = self.pool_swap.from_script;
        let to = self.pool_swap.to_script;
        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            let swap = model::PoolSwap {
                id: format!("{}-{}", pool_id, txid),
                sort: format!("{}-{}", ctx.block.height, ctx.tx_idx),
                txid,
                txno: ctx.tx_idx,
                from_amount: self.pool_swap.from_amount,
                from_token_id: self.pool_swap.from_token_id.0,
                to_token_id: self.pool_swap.to_token_id.0,
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
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            services
                .pool
                .by_id
                .delete(&(pool_id, ctx.block.height, ctx.tx_idx))?;
        }
        tx_result::invalidate(services, &ctx.tx.txid)
    }
}