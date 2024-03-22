use ain_db::{version::Migration, DBError};
use anyhow::format_err;
use rayon::prelude::*;

use super::{block_store::BlockStore, db::columns};
use crate::Result;
use ain_db::Result as DBResult;

/// Migration for version 1.
/// Context:
/// Release v4.0.1
/// Remove duplicate TransactionV2 entries in storage and store block hash and tx index instead.
pub struct MigrationV1;

impl Migration<BlockStore> for MigrationV1 {
    fn version(&self) -> u32 {
        1
    }

    fn migrate(&self, store: &BlockStore) -> DBResult<()> {
        self.migrate_transactions(store)
            .map_err(|e| DBError::Custom(format_err!("{e}")))?;
        Ok(())
    }
}

impl MigrationV1 {
    /// Migrates transactions to be associated with their respective block hashes and indexes.
    fn migrate_transactions(&self, store: &BlockStore) -> Result<()> {
        let transactions_cf = store.column::<columns::Transactions>();
        let blocks_cf = store.column::<columns::Blocks>();

        blocks_cf
            .iter(None, rocksdb::Direction::Forward)?
            .par_bridge()
            .try_for_each(|el| {
                let (_, block) = el?;
                let block_hash = block.header.hash();
                block
                    .transactions
                    .par_iter()
                    .enumerate()
                    .try_for_each(|(index, transaction)| {
                        transactions_cf.put(&transaction.hash(), &(block_hash, index))
                    })
            })?;

        Ok(())
    }
}
