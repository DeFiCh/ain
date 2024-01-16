use log::debug;

use super::BlockContext;
use crate::{
    indexer::{Index, Result},
    model::Transaction,
    repository::RepositoryOps,
    SERVICES,
};

// impl Index for CreateTransaction {
//     fn index(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
//         debug!("[CreateMasternode] Indexing...");
//         let txid = tx.txid();
//         SERVICES.transaction.by_id.put(&txid, &tx)?;
//     }

//     fn invalidate(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
//         debug!("[CreateMasternode] Invalidating...");
//         SERVICES.transaction.by_id.delete(&tx.txid())?;
//     }
// }
