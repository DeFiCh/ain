use dftx_rs::{pool::*, Transaction};

use super::BlockContext;
use crate::indexer::{Index, Result};

impl Index for PoolSwap {
    fn index(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }
}

impl Index for CompositeSwap {
    fn index(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }
}
