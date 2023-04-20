use crate::cache::Cache;
use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H256, U256};

#[derive(Debug)]
pub struct Storage {
    cache: Cache,
}

// TODO : Add DB and pull from DB when cache miss
impl Default for Storage {
    fn default() -> Self {
        Self::new()
    }
}

impl Storage {
    pub fn new() -> Self {
        Self {
            cache: Cache::new(None),
        }
    }
}

// Block storage
impl Storage {
    pub fn get_block_by_number(&self, number: &U256) -> Option<BlockAny> {
        self.cache.get_block_by_number(number)
    }

    pub fn get_block_by_hash(&self, block_hash: &H256) -> Option<BlockAny> {
        self.cache.get_block_by_hash(block_hash)
    }

    pub fn put_block(&self, block: BlockAny) {
        self.cache.put_block(block)
    }
}

// Transaction storage
impl Storage {
    pub fn get_transaction_by_hash(&self, hash: H256) -> Option<TransactionV2> {
        self.cache.get_transaction_by_hash(&hash)
    }

    pub fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> Option<TransactionV2> {
        self.cache
            .get_transaction_by_block_hash_and_index(&hash, index)
    }

    pub fn get_transaction_by_block_number_and_index(
        &self,
        hash: U256,
        index: usize,
    ) -> Option<TransactionV2> {
        self.cache
            .get_transaction_by_block_number_and_index(&hash, index)
    }
}

// Latest block storage
impl Storage {
    pub fn get_latest_block(&self) -> Option<BlockAny> {
        self.cache.get_latest_block()
    }

    pub fn put_latest_block(&self, block: BlockAny) {
        self.cache.put_latest_block(block)
    }
}
