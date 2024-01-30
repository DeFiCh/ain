use dftx_rs::{oracles::*, Transaction};

use super::BlockContext;
use crate::indexer::{Index, Result};

impl Index for AppointOracle {
    fn index(&self, ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }
}

impl Index for RemoveOracle {
    fn index(&self, ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }
}

impl Index for UpdateOracle {
    fn index(&self, ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }
}

impl Index for SetOracleData {
    fn index(&self, ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }
}
