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
        let id_a = self.token_a.0 as u32;
        let id_b = self.token_b.0 as u32;

        services
            .poolpair
            .by_height
            .put(&(ctx.block.height, ctx.tx_idx), &(pool_id, id_a, id_b))?;

        services.poolpair.by_id.put(&(id_a, id_b), &pool_id)?;
        services.poolpair.by_id.put(&(id_b, id_a), &pool_id)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        if let Some((_, id_a, id_b)) = services
            .poolpair
            .by_height
            .get(&(ctx.block.height, ctx.tx_idx))?
        {
            services.poolpair.by_id.delete(&(id_a, id_b))?;
            services.poolpair.by_id.delete(&(id_b, id_a))?;
        }

        services
            .poolpair
            .by_height
            .delete(&(ctx.block.height, ctx.tx_idx))?;

        Ok(())
    }
}
