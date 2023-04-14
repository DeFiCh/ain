use std::{num::NonZeroUsize, sync::RwLock};

use ethereum::{BlockAny, TransactionV2};
use lru::LruCache;
use primitive_types::{H256, U256};
use std::borrow::ToOwned;

#[derive(Debug)]
pub struct Cache {
    transactions: RwLock<LruCache<H256, TransactionV2>>,
    blocks: RwLock<LruCache<U256, BlockAny>>,
    block_hashes: RwLock<LruCache<H256, U256>>,
    latest_block: RwLock<Option<BlockAny>>,
}

impl Cache {
    const DEFAULT_CACHE_SIZE: usize = 1000;

    pub fn new(cache_size: Option<usize>) -> Self {
        Cache {
            transactions: RwLock::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            blocks: RwLock::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            block_hashes: RwLock::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            latest_block: RwLock::new(None),
        }
    }
}

impl Cache {
    pub fn extend_transactions_from_block(&self, block: &BlockAny) {
        let mut cache = self.transactions.write().unwrap();

        for transaction in &block.transactions {
            let hash = transaction.hash();
            cache.put(hash, transaction.clone());
        }
    }

    pub fn get_transaction_by_hash(&self, hash: &H256) -> Option<TransactionV2> {
        self.transactions
            .write()
            .unwrap()
            .get(hash)
            .map(ToOwned::to_owned)
    }

    pub fn get_transaction_by_block_hash_and_index(
        &self,
        block_hash: &H256,
        index: usize,
    ) -> Option<TransactionV2> {
        self.block_hashes
            .write()
            .unwrap()
            .get(block_hash)
            .and_then(|block_number| {
                self.get_transaction_by_block_number_and_index(&block_number, index)
            })
    }

    pub fn get_transaction_by_block_number_and_index(
        &self,
        block_number: &U256,
        index: usize,
    ) -> Option<TransactionV2> {
        self.blocks
            .write()
            .unwrap()
            .get(block_number)?
            .transactions
            .get(index)
            .map(ToOwned::to_owned)
    }
}

// Block impl
impl Cache {
    pub fn get_block_by_number(&self, number: &U256) -> Option<BlockAny> {
        self.blocks
            .write()
            .unwrap()
            .get(number)
            .map(ToOwned::to_owned)
    }

    pub fn get_block_by_hash(&self, block_hash: &H256) -> Option<BlockAny> {
        self.block_hashes
            .write()
            .unwrap()
            .get(block_hash)
            .and_then(|block_number| self.get_block_by_number(&block_number))
    }

    pub fn put_block(&self, block: BlockAny) {
        self.extend_transactions_from_block(&block);

        let block_number = block.header.number;
        let hash = block.header.hash();
        self.blocks.write().unwrap().put(block_number, block);
        self.block_hashes.write().unwrap().put(hash, block_number);
    }
}

// Latest block impl
impl Cache {
    pub fn get_latest_block(&self) -> Option<BlockAny> {
        self.latest_block
            .read()
            .unwrap()
            .as_ref()
            .map(ToOwned::to_owned)
    }

    pub fn put_latest_block(&self, block: BlockAny) {
        let mut cache = self.latest_block.write().unwrap();
        *cache = Some(block);
    }
}
