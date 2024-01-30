use dftx_rs::{vault::PlaceAuctionBid, Transaction};
use log::debug;

use super::BlockContext;
use crate::{
    indexer::{Index, Result},
    model::VaultAuctionBatchHistory,
    repository::RepositoryOps,
    SERVICES,
};

impl Index for PlaceAuctionBid {
    fn index(&self, ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
        debug!("[PlaceAuctionBid] Indexing...");

        let auction = VaultAuctionBatchHistory {
            id: format!("{}-{}-{}", self.vault_id, self.index, tx.txid()),
            key: format!("{}-{}", self.vault_id, self.index),
            sort: format!("{}-{}", ctx.height, idx),
            vault_id: self.vault_id,
            index: idx,
            from: self.from.clone(),
            amount: self.token_amount.amount,
            token_id: self.token_amount.token.0,
            block: ctx.clone(),
        };
        debug!("auction : {:?}", auction);

        let key = (self.vault_id, self.index, tx.txid());
        SERVICES.auction.by_id.put(&key, &auction)?;
        SERVICES
            .auction
            .by_height
            .put(&(self.vault_id, self.index, ctx.height, idx), &key)
    }

    fn invalidate(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        debug!("[PlaceAuctionBid] Invalidating...");
        SERVICES
            .auction
            .by_id
            .delete(&(self.vault_id, self.index, tx.txid()))?;
        SERVICES
            .auction
            .by_height
            .delete(&(self.vault_id, self.index, ctx.height, idx))
    }
}
