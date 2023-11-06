use rayon::prelude::*;

use super::{block_store::BlockStore, db::columns};
use crate::Result;

pub trait Migration {
    fn version(&self) -> u32;
    fn migrate(&self, store: &BlockStore) -> Result<()>;
}

pub struct MigrationV1;
impl Migration for MigrationV1 {
    fn version(&self) -> u32 {
        1
    }

    fn migrate(&self, store: &BlockStore) -> Result<()> {
        self.migrate_transactions(store)?;
        Ok(())
    }
}

impl MigrationV1 {
    fn migrate_transactions(&self, store: &BlockStore) -> Result<()> {
        let transactions_cf = store.column::<columns::Transactions>();
        let blocks_cf = store.column::<columns::Blocks>();

        blocks_cf
            .iter(None, None)
            .par_bridge()
            .try_for_each(|(_, block)| {
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
