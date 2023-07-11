mod cache;
mod code;
mod data_handler;
pub mod traits;

use crate::log::LogIndex;
use ethereum::{BlockAny, TransactionV2};
use crate::{MaybeTransactionV2, MaybeBlockAny};
use primitive_types::{H160, H256, U256};
use std::collections::HashMap;

use crate::receipt::Receipt;
use crate::storage::traits::LogStorage;

use self::{
    cache::Cache,
    data_handler::BlockchainDataHandler,
    traits::{
        BlockStorage, FlushableStorage, ReceiptStorage, Rollback,
        TransactionStorage,
    },
};

use crate::Result;

#[derive(Debug)]
pub struct Storage {
    cache: Cache,
    blockchain_data_handler: BlockchainDataHandler,
}

impl Default for Storage {
    fn default() -> Self {
        Self::new()
    }
}

impl Storage {
    pub fn new() -> Self {
        Self {
            cache: Cache::new(None),
            blockchain_data_handler: BlockchainDataHandler::default(),
        }
    }

    pub fn restore() -> Self {
        Self {
            cache: Cache::new(None),
            blockchain_data_handler: BlockchainDataHandler::restore(),
        }
    }
}

impl BlockStorage for Storage {
    fn get_block_by_number(&self, number: &U256) -> Result<MaybeBlockAny> {
        self.cache.get_block_by_number(number).or_else(|| {
            let block = self.blockchain_data_handler.get_block_by_number(number);
            if let Some(ref block) = block {
                self.cache.put_block(block);
            }
            block
        })
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Result<MaybeBlockAny> {
        self.cache.get_block_by_hash(block_hash).or_else(|| {
            let block = self.blockchain_data_handler.get_block_by_hash(block_hash);
            if let Some(ref block) = block {
                self.cache.put_block(block);
            }
            block
        })
    }

    fn put_block(&self, block: &BlockAny) -> Result<()> {
        self.cache.put_block(block)?;
        self.blockchain_data_handler.put_block(block)?;
        Ok(())
    }

    fn get_latest_block(&self) -> Result<MaybeBlockAny> {
        let res = self.cache.get_latest_block().or_else(|| {
            let latest_block = self.blockchain_data_handler.get_latest_block();
            if let Some(ref block) = latest_block {
                self.cache.put_latest_block(Some(block))?;
            }
            latest_block
        });
        Ok(res)
    }

    fn put_latest_block(&self, block: &BlockAny) -> Result<()> {
        self.cache.put_latest_block(block)?;
        self.blockchain_data_handler.put_latest_block(block)?;
        Ok(())
    }

    fn get_base_fee(&self, block_hash: &H256) -> Result<Option<U256>> {
        self.cache.get_base_fee(block_hash).or_else(|| {
            let base_fee = self.blockchain_data_handler.get_base_fee(block_hash);
            if let Some(base_fee) = base_fee {
                self.cache.set_base_fee(*block_hash, base_fee);
            }
            base_fee
        })
    }

    fn set_base_fee(&self, block_hash: H256, base_fee: U256) -> Result<()> {
        self.cache.set_base_fee(block_hash, base_fee);
        self.blockchain_data_handler
            .set_base_fee(block_hash, base_fee);
    }
}

impl TransactionStorage for Storage {
    fn extend_transactions_from_block(&self, block: &BlockAny) -> Result<()> {
        // Feature flag
        self.cache.extend_transactions_from_block(block);

        self.blockchain_data_handler
            .extend_transactions_from_block(block);
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Result<MaybeTransactionV2> {
        self.cache.get_transaction_by_hash(hash).or_else(|| {
            let transaction = self.blockchain_data_handler.get_transaction_by_hash(hash);
            if let Some(ref transaction) = transaction {
                self.cache.put_transaction(transaction);
            }
            transaction
        })
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: &H256,
        index: usize,
    ) -> Result<MaybeTransactionV2> {
        self.cache
            .get_transaction_by_block_hash_and_index(hash, index)
            .or_else(|| {
                let transaction = self
                    .blockchain_data_handler
                    .get_transaction_by_block_hash_and_index(hash, index);
                if let Some(ref transaction) = transaction {
                    self.cache.put_transaction(transaction)
                }
                transaction
            })
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        number: &U256,
        index: usize,
    ) -> Result<MaybeTransactionV2> {
        let res = self.cache
            .get_transaction_by_block_number_and_index(number, index)
            .or_else(|| {
                let transaction = self
                    .blockchain_data_handler
                    .get_transaction_by_block_number_and_index(number, index)?;
                if let Some(ref transaction) = transaction {
                    self.cache.put_transaction(transaction)?
                }
                transaction
            });
        Ok(res)
    }

    fn put_transaction(&self, transaction: &TransactionV2) -> Result<()> {
        self.cache.put_transaction(transaction)?;
        self.blockchain_data_handler.put_transaction(transaction)?;
        Ok(())
    }
}

impl ReceiptStorage for Storage {
    fn get_receipt(&self, tx: &H256) -> Option<Receipt> {
        self.blockchain_data_handler.get_receipt(tx)
    }

    fn put_receipts(&self, receipts: Vec<Receipt>) {
        self.blockchain_data_handler.put_receipts(receipts)
    }
}

impl LogStorage for Storage {
    fn get_logs(&self, block_number: &U256) -> Option<HashMap<H160, Vec<LogIndex>>> {
        self.blockchain_data_handler.get_logs(block_number)
    }

    fn put_logs(&self, address: H160, logs: Vec<LogIndex>, block_number: U256) {
        self.blockchain_data_handler
            .put_logs(address, logs, block_number)
    }
}

impl FlushableStorage for Storage {
    fn flush(&self) -> Result<()> {
        self.blockchain_data_handler.flush()
    }
}

impl Storage {
    pub fn get_code_by_hash(&self, hash: H256) -> Option<Vec<u8>> {
        self.blockchain_data_handler.get_code_by_hash(&hash)
    }

    pub fn put_code(&self, hash: H256, code: Vec<u8>) {
        self.blockchain_data_handler.put_code(&hash, &code)
    }
}

impl Storage {
    pub fn dump_db(&self) {
        println!(
            "self.block_data_handler : {:#?}",
            self.blockchain_data_handler
        );
    }
}

impl Rollback for Storage {
    fn disconnect_latest_block(&self) -> Result<()> {
        self.cache.disconnect_latest_block()?; 
        self.blockchain_data_handler.disconnect_latest_block()?;
        Ok(())
    }
}
