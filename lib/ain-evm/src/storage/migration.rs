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
        for (_, block) in blocks_cf.iter(None, None) {
            let block_hash = block.header.hash();
            for (index, transaction) in block.transactions.iter().enumerate() {
                transactions_cf.put(&transaction.hash(), &(block_hash, index))?
            }
        }
        Ok(())
    }
}
