use std::{borrow::ToOwned, num::NonZeroUsize, sync::RwLock};

use ethereum::{BlockAny, TransactionV2};
use ethereum_types::{H256, U256};
use lru::LruCache;
use parking_lot::Mutex;

use super::traits::{BlockStorage, Rollback, TransactionStorage};
use crate::Result;

#[derive(Debug)]
pub struct Cache {
    transactions: Mutex<LruCache<H256, TransactionV2>>,
    blocks: Mutex<LruCache<U256, BlockAny>>,
    block_hashes: Mutex<LruCache<H256, U256>>,
    latest_block: RwLock<Option<BlockAny>>,
    contract_code: Mutex<LruCache<H256, Vec<u8>>>,
}

impl Cache {
    const DEFAULT_CACHE_SIZE: usize = 1000;

    pub fn new(cache_size: Option<usize>) -> Self {
        Cache {
            transactions: Mutex::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            blocks: Mutex::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            block_hashes: Mutex::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            contract_code: Mutex::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            latest_block: RwLock::new(None),
        }
    }

    pub fn put_transaction(&self, transaction: &TransactionV2) -> Result<()> {
        self.transactions
            .lock()
            .put(transaction.hash(), transaction.clone());
        Ok(())
    }
}

impl BlockStorage for Cache {
    fn get_block_by_number(&self, number: &U256) -> Result<Option<BlockAny>> {
        let block = self.blocks.lock().get(number).map(ToOwned::to_owned);
        Ok(block)
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Result<Option<BlockAny>> {
        self.block_hashes
            .lock()
            .get(block_hash)
            .map_or(Ok(None), |block_number| {
                self.get_block_by_number(block_number)
            })
    }

    fn put_block(&self, block: &BlockAny) -> Result<()> {
        self.put_transactions_from_block(block)?;

        let block_number = block.header.number;
        let hash = block.header.hash();
        self.blocks.lock().put(block_number, block.clone());
        self.block_hashes.lock().put(hash, block_number);
        Ok(())
    }

    fn get_latest_block(&self) -> Result<Option<BlockAny>> {
        let block = self
            .latest_block
            .read()
            .unwrap()
            .as_ref()
            .map(ToOwned::to_owned);
        Ok(block)
    }

    fn put_latest_block(&self, block: Option<&BlockAny>) -> Result<()> {
        let mut cache = self.latest_block.write().unwrap();
        *cache = block.cloned();
        Ok(())
    }
}

impl TransactionStorage for Cache {
    fn put_transactions_from_block(&self, block: &BlockAny) -> Result<()> {
        let mut cache = self.transactions.lock();

        for transaction in &block.transactions {
            let hash = transaction.hash();
            cache.put(hash, transaction.clone());
        }
        Ok(())
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Result<Option<TransactionV2>> {
        let transaction = self.transactions.lock().get(hash).map(ToOwned::to_owned);
        Ok(transaction)
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        block_hash: &H256,
        index: usize,
    ) -> Result<Option<TransactionV2>> {
        self.block_hashes
            .lock()
            .get(block_hash)
            .map_or(Ok(None), |block_number| {
                self.get_transaction_by_block_number_and_index(block_number, index)
            })
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: &U256,
        index: usize,
    ) -> Result<Option<TransactionV2>> {
        let transaction = self
            .blocks
            .lock()
            .get(block_number)
            .and_then(|block| block.transactions.get(index).map(ToOwned::to_owned));
        Ok(transaction)
    }
}

impl Rollback for Cache {
    fn disconnect_latest_block(&self) -> Result<()> {
        if let Some(block) = self.get_latest_block()? {
            let mut transaction_cache = self.transactions.lock();
            for tx in &block.transactions {
                transaction_cache.pop(&tx.hash());
            }

            self.block_hashes.lock().pop(&block.header.hash());
            self.blocks.lock().pop(&block.header.number);

            let previous_block = self.get_block_by_hash(&block.header.parent_hash)?;
            self.put_latest_block(previous_block.as_ref())?;
        }
        Ok(())
    }
}

impl Cache {
    pub fn get_code_by_hash(&self, hash: &H256) -> Result<Option<Vec<u8>>> {
        Ok(self.contract_code.lock().get(hash).map(ToOwned::to_owned))
    }

    pub fn put_code(&self, hash: H256, code: &[u8]) -> Result<()> {
        self.contract_code.lock().put(hash, code.to_vec());
        Ok(())
    }
}
