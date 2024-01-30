use dftx_rs::{pool::*, Transaction};
use log::debug;

use crate::{
    indexer::{tx_result, Index, Result},
    model::{self, BlockContext, PoolSwapResult, TxResult},
    repository::RepositoryOps,
    SERVICES,
};

impl Index for PoolSwap {
    fn index(&self, ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
        debug!("[Poolswap] Indexing...");
        let txid = tx.txid();
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) =
            SERVICES.result.get(&txid)?
        else {
            println!("Missing swap result for {}", tx.txid().to_string());
            return Err("Missing swap result".into());
        };

        let swap = model::PoolSwap {
            id: format!("{}-{}", pool_id, txid),
            sort: format!("{}-{}", ctx.height, idx),
            txid: txid,
            txno: idx,
            from_amount: self.from_amount,
            from_token_id: self.from_token_id.0,
            to_token_id: self.to_token_id.0,
            to_amount,
            pool_id,
            from: self.from_script.clone(),
            to: self.to_script.clone(),
            block: ctx.clone(),
        };
        debug!("swap : {:?}", swap);

        SERVICES.pool.by_id.put(&(pool_id, ctx.height, idx), &swap)
    }

    fn invalidate(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        let txid = tx.txid();
        let Some(TxResult::PoolSwap(PoolSwapResult { pool_id, .. })) =
            SERVICES.result.get(&txid)?
        else {
            return Err("Missing swap result".into());
        };

        SERVICES.pool.by_id.delete(&(pool_id, ctx.height, idx))?;
        tx_result::invalidate(&txid)
    }
}

impl Index for CompositeSwap {
    fn index(&self, ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
        debug!("[CompositeSwap] Indexing...");
        let txid = tx.txid();
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, .. })) =
            SERVICES.result.get(&txid)?
        else {
            println!("Missing swap result for {}", txid.to_string());
            return Err("Missing swap result".into());
        };

        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            let swap = model::PoolSwap {
                id: format!("{}-{}", pool_id, txid),
                sort: format!("{}-{}", ctx.height, idx),
                txid: txid,
                txno: idx,
                from_amount: self.pool_swap.from_amount,
                from_token_id: self.pool_swap.from_token_id.0,
                to_token_id: self.pool_swap.to_token_id.0,
                to_amount,
                pool_id,
                from: self.pool_swap.from_script.clone(),
                to: self.pool_swap.to_script.clone(),
                block: ctx.clone(),
            };
            debug!("swap : {:?}", swap);
            SERVICES
                .pool
                .by_id
                .put(&(pool_id, ctx.height, idx), &swap)?;
        }

        Ok(())
    }

    fn invalidate(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            SERVICES.pool.by_id.delete(&(pool_id, ctx.height, idx))?;
        }
        tx_result::invalidate(&tx.txid())
    }
}
