use std::{num::NonZeroUsize, sync::RwLock};

use ethereum::{BlockAny, TransactionV2};
use lru::LruCache;
use primitive_types::{H256, U256};
use std::borrow::ToOwned;

use super::traits::{BlockStorage, Rollback, TransactionStorage};
use crate::{Result, Ok, MaybeBlockAny, MaybeTransactionV2};

#[derive(Debug)]
pub struct Cache {
    transactions: RwLock<LruCache<H256, TransactionV2>>,
    blocks: RwLock<LruCache<U256, BlockAny>>,
    block_hashes: RwLock<LruCache<H256, U256>>,
    base_fee: RwLock<LruCache<H256, U256>>,
    latest_block: RwLock<MaybeBlockAny>,
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
            base_fee: RwLock::new(LruCache::new(
                NonZeroUsize::new(cache_size.unwrap_or(Self::DEFAULT_CACHE_SIZE)).unwrap(),
            )),
            latest_block: RwLock::new(None),
        }
    }
}

impl BlockStorage for Cache {
    fn get_block_by_number(&self, number: &U256) -> Result<MaybeBlockAny> {
        let res = self.blocks
            .write()?
            .get(number)
            .map(ToOwned::to_owned);
        Ok(res)
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Result<MaybeBlockAny> {
        let res = self.block_hashes
            .write()?
            .get(block_hash)
            .and_then(|block_number| self.get_block_by_number(block_number))?;
        Ok(res)
    }

    fn put_block(&self, block: &BlockAny) -> Result<()> {
        self.extend_transactions_from_block(block);

        let block_number = block.header.number;
        let hash = block.header.hash();
        self.blocks
            .write()
            .unwrap()
            .put(block_number, block.clone());
        self.block_hashes.write()?.put(hash, block_number);

        Ok(())
    }

    fn get_latest_block(&self) -> Result<MaybeBlockAny> {
        let res = self.latest_block
            .read()?
            .as_ref()
            .map(ToOwned::to_owned);

        Ok(res)
    }

    fn put_latest_block(&self, block: &BlockAny) -> Result<()> {
        let mut cache = self.latest_block.write()?;
        *cache = block.cloned();
        Ok(())
    }

    fn get_base_fee(&self, block_hash: &H256) -> Result<Option<U256>> {
        let res = self.base_fee
            .write()?
            .get(block_hash)
            .map(ToOwned::to_owned);
        Ok(res)
    }

    fn set_base_fee(&self, block_hash: H256, base_fee: U256) -> Result<()> {
        let mut cache = self.base_fee.write()?;
        cache.put(block_hash, base_fee);
        Ok(())
    }
}

impl TransactionStorage for Cache {
    fn extend_transactions_from_block(&self, block: &BlockAny) -> Result<()> {
        let mut cache = self.transactions.write()?;

        for transaction in &block.transactions {
            let hash = transaction.hash();
            cache.put(hash, transaction.clone());
        }

        Ok(())
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Result<MaybeTransactionV2> {
        let res = self.transactions
            .write()?
            .get(hash)
            .map(ToOwned::to_owned);
        Ok(res)
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        block_hash: &H256,
        index: usize,
    ) -> Result<MaybeTransactionV2> {
        let res = self.block_hashes
            .write()?
            .get(block_hash)
            .and_then(|block_number| {
                self.get_transaction_by_block_number_and_index(block_number, index)
            })?;
        Ok(res)
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: &U256,
        index: usize,
    ) -> Result<MaybeTransactionV2> {
        let res = self.blocks
            .write()?
            .get(block_number)?
            .transactions
            .get(index)
            .map(ToOwned::to_owned);
        Ok(res)
    }

    fn put_transaction(&self, transaction: &TransactionV2) -> Result<()> {
        let res = self.transactions
            .write()?
            .put(transaction.hash(), transaction.clone());
        Ok(res)
    }
}

impl Rollback for Cache {
    fn disconnect_latest_block(&self) -> Result<()> {
        let block = self.get_latest_block()?;
        if let Some(block) = block {
            let mut transaction_cache = self.transactions.write()?;
            for tx in &block.transactions {
                transaction_cache.pop(&tx.hash());
            }

            self.block_hashes.write()?.pop(&block.header.hash());
            self.blocks.write()?.pop(&block.header.number);

            let b = self.get_block_by_hash(&block.header.parent_hash)?.as_ref()?;
            self.put_latest_block(b)?
        }
        Ok(())
    }
}
