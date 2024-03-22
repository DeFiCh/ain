pub mod block_store;
mod cache;
mod db;
mod migration;
pub mod traits;

use std::{collections::HashMap, path::Path};

use ethereum::{BlockAny, TransactionV2};
use ethereum_types::{H160, H256, U256};

use self::{
    block_store::{BlockStore, DumpArg},
    cache::Cache,
    traits::{BlockStorage, FlushableStorage, ReceiptStorage, Rollback, TransactionStorage},
};
use crate::{log::LogIndex, receipt::Receipt, storage::traits::LogStorage, Result};

#[derive(Debug)]
pub struct Storage {
    cache: Cache,
    blockstore: BlockStore,
}

impl Storage {
    pub fn new(path: &Path) -> Result<Self> {
        Ok(Self {
            cache: Cache::new(None),
            blockstore: BlockStore::new(path)?,
        })
    }

    pub fn restore(path: &Path) -> Result<Self> {
        Ok(Self {
            cache: Cache::new(None),
            blockstore: BlockStore::new(path)?,
        })
    }
}

impl BlockStorage for Storage {
    fn get_block_by_number(&self, number: &U256) -> Result<Option<BlockAny>> {
        match self.cache.get_block_by_number(number) {
            Ok(Some(block)) => Ok(Some(block)),
            Ok(None) => {
                let block = self.blockstore.get_block_by_number(number);
                if let Ok(Some(ref block)) = block {
                    self.cache.put_block(block)?;
                }
                block
            }
            Err(e) => Err(e),
        }
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Result<Option<BlockAny>> {
        match self.cache.get_block_by_hash(block_hash) {
            Ok(Some(block)) => Ok(Some(block)),
            Ok(None) => {
                let block = self.blockstore.get_block_by_hash(block_hash);
                if let Ok(Some(ref block)) = block {
                    self.cache.put_block(block)?;
                }
                block
            }
            Err(e) => Err(e),
        }
    }

    fn put_block(&self, block: &BlockAny) -> Result<()> {
        self.blockstore.put_block(block)
    }

    fn get_latest_block(&self) -> Result<Option<BlockAny>> {
        match self.cache.get_latest_block() {
            Ok(Some(block)) => Ok(Some(block)),
            Ok(None) => {
                let block = self.blockstore.get_latest_block();
                if let Ok(Some(ref block)) = block {
                    self.cache.put_latest_block(Some(block))?;
                }
                block
            }
            Err(e) => Err(e),
        }
    }

    fn put_latest_block(&self, block: Option<&BlockAny>) -> Result<()> {
        self.cache.put_latest_block(block)?;
        self.blockstore.put_latest_block(block)
    }
}

impl TransactionStorage for Storage {
    fn put_transactions_from_block(&self, block: &BlockAny) -> Result<()> {
        self.blockstore.put_transactions_from_block(block)
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Result<Option<TransactionV2>> {
        match self.cache.get_transaction_by_hash(hash) {
            Ok(Some(transaction)) => Ok(Some(transaction)),
            Ok(None) => {
                let transaction = self.blockstore.get_transaction_by_hash(hash);
                if let Ok(Some(ref transaction)) = transaction {
                    self.cache.put_transaction(transaction)?;
                }
                transaction
            }
            Err(e) => Err(e),
        }
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: &H256,
        index: usize,
    ) -> Result<Option<TransactionV2>> {
        match self
            .cache
            .get_transaction_by_block_hash_and_index(hash, index)
        {
            Ok(Some(transaction)) => Ok(Some(transaction)),
            Ok(None) => {
                let transaction = self
                    .blockstore
                    .get_transaction_by_block_hash_and_index(hash, index);
                if let Ok(Some(ref transaction)) = transaction {
                    self.cache.put_transaction(transaction)?;
                }
                transaction
            }
            Err(e) => Err(e),
        }
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        number: &U256,
        index: usize,
    ) -> Result<Option<TransactionV2>> {
        match self
            .cache
            .get_transaction_by_block_number_and_index(number, index)
        {
            Ok(Some(transaction)) => Ok(Some(transaction)),
            Ok(None) => {
                let transaction = self
                    .blockstore
                    .get_transaction_by_block_number_and_index(number, index);
                if let Ok(Some(ref transaction)) = transaction {
                    self.cache.put_transaction(transaction)?;
                }
                transaction
            }
            Err(e) => Err(e),
        }
    }
}

impl ReceiptStorage for Storage {
    fn get_receipt(&self, tx: &H256) -> Result<Option<Receipt>> {
        self.blockstore.get_receipt(tx)
    }

    fn put_receipts(&self, receipts: Vec<Receipt>) -> Result<()> {
        self.blockstore.put_receipts(receipts)
    }
}

impl LogStorage for Storage {
    fn get_logs(&self, block_number: &U256) -> Result<Option<HashMap<H160, Vec<LogIndex>>>> {
        self.blockstore.get_logs(block_number)
    }

    fn put_logs(&self, address: H160, logs: Vec<LogIndex>, block_number: U256) -> Result<()> {
        self.blockstore.put_logs(address, logs, block_number)
    }
}

impl FlushableStorage for Storage {
    fn flush(&self) -> Result<()> {
        self.blockstore.flush()
    }
}

impl Storage {
    pub fn get_code_by_hash(&self, address: H160, hash: H256) -> Result<Option<Vec<u8>>> {
        match self.cache.get_code_by_hash(&hash) {
            Ok(Some(code)) => Ok(Some(code)),
            Ok(None) => {
                let code = self.blockstore.get_code_by_hash(address, &hash);
                if let Ok(Some(ref code)) = code {
                    self.cache.put_code(hash, code)?;
                }
                code
            }
            Err(e) => Err(e),
        }
    }

    pub fn put_code(
        &self,
        block_number: U256,
        address: H160,
        hash: H256,
        code: Vec<u8>,
    ) -> Result<()> {
        self.blockstore
            .put_code(block_number, address, &hash, &code)
    }
}

impl Storage {
    pub fn dump_db(&self, arg: DumpArg, from: Option<&str>, limit: usize) -> Result<String> {
        self.blockstore.dump(&arg, from, limit)
    }
}

impl Rollback for Storage {
    fn disconnect_latest_block(&self) -> Result<()> {
        self.cache.disconnect_latest_block()?;
        self.blockstore.disconnect_latest_block()
    }
}
