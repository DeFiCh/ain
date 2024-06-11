use std::sync::Arc;

use ain_dftx::pool::CreatePoolPair;
use log::debug;

use crate::{
    indexer::{Context, Index, Result},
    model::TxResult,
    repository::RepositoryOps,
    Services,
};

impl Index for CreatePoolPair {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let Some(TxResult::CreatePoolPair(pool_id)) = services.result.get(&ctx.tx.txid)? else {
            return Err("[CreatePoolPair] Missing result".into());
        };

        debug!(
            "[CreatePoolPair] Indexing {} {} id {}",
            ctx.block.height, ctx.tx_idx, &pool_id
        );
        services
            .poolpair
            .put(&(ctx.block.height, ctx.tx_idx), &pool_id)?;
        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        services.poolpair.delete(&(ctx.block.height, ctx.tx_idx))?;
        Ok(())
    }
}
