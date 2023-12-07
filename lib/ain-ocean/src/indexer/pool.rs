use dftx_rs::{pool::*, Transaction};

use super::BlockContext;
use crate::indexer::{Index, Result};

impl Index for PoolSwap {
    fn index(&self, context: &BlockContext, tx: Transaction) -> Result<()> {
        todo!()
    }

    fn invalidate(&self) {
        todo!()
    }
}

impl Index for CompositeSwap {
    fn index(&self, context: &BlockContext, tx: Transaction) -> Result<()> {
        todo!()
    }

    fn invalidate(&self) {
        todo!()
    }
}
