use dftx_rs::{oracles::*, Transaction};

use super::BlockContext;
use crate::indexer::{Index, Result};

impl Index for AppointOracle {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self) {
        todo!()
    }
}

impl Index for RemoveOracle {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self) {
        todo!()
    }
}

impl Index for UpdateOracle {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self) {
        todo!()
    }
}

impl Index for SetOracleData {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        todo!()
    }

    fn invalidate(&self) {
        todo!()
    }
}
