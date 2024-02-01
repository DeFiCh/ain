use std::sync::Arc;

use dftx_rs::vault::PlaceAuctionBid;
use log::debug;

use super::Context;
use crate::{
    indexer::{Index, Result},
    model::VaultAuctionBatchHistory,
    repository::RepositoryOps,
    Services,
};

impl Index for PlaceAuctionBid {
    fn index(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[PlaceAuctionBid] Indexing...");

        let auction = VaultAuctionBatchHistory {
            id: format!("{}-{}-{}", self.vault_id, self.index, ctx.tx.txid),
            key: format!("{}-{}", self.vault_id, self.index),
            sort: format!("{}-{}", ctx.block.height, ctx.tx_idx),
            vault_id: self.vault_id,
            index: ctx.tx_idx,
            from: self.from.clone(),
            amount: self.token_amount.amount,
            token_id: self.token_amount.token.0,
            block: ctx.block.clone(),
        };
        debug!("auction : {:?}", auction);

        let key = (self.vault_id, self.index, ctx.tx.txid);
        services.auction.by_id.put(&key, &auction)?;
        services.auction.by_height.put(
            &(self.vault_id, self.index, ctx.block.height, ctx.tx_idx),
            &key,
        )
    }

    fn invalidate(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[PlaceAuctionBid] Invalidating...");
        services
            .auction
            .by_id
            .delete(&(self.vault_id, self.index, ctx.tx.txid))?;
        services.auction.by_height.delete(&(
            self.vault_id,
            self.index,
            ctx.block.height,
            ctx.tx_idx,
        ))
    }
}
