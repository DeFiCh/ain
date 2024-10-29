use std::sync::Arc;

use ain_dftx::vault::PlaceAuctionBid;
use log::trace;

use super::Context;
use crate::{
    indexer::{Index, Result},
    model::VaultAuctionBatchHistory,
    storage::RepositoryOps,
    Services,
};

impl Index for PlaceAuctionBid {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        trace!("[PlaceAuctionBid] Indexing...");

        let auction = VaultAuctionBatchHistory {
            from: self.from,
            amount: self.token_amount.amount,
            token_id: self.token_amount.token.0,
            block: ctx.block.clone(),
        };
        trace!("auction : {:?}", auction);

        let id = (
            self.vault_id,
            self.index.to_be_bytes(),
            ctx.block.height.to_be_bytes(),
            ctx.tx.txid,
        );
        services.auction.by_id.put(&id, &auction)
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        trace!("[PlaceAuctionBid] Invalidating...");
        services.auction.by_id.delete(&(
            self.vault_id,
            self.index.to_be_bytes(),
            ctx.block.height.to_be_bytes(),
            ctx.tx.txid,
        ))
    }
}
