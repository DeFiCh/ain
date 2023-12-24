use dftx_rs::{vault::PlaceAuctionBid, Transaction};

use super::BlockContext;
use crate::indexer::{Index, Result};

impl Index for PlaceAuctionBid {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self) {
        todo!()
    }
}
