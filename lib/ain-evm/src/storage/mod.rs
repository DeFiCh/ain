mod cache;
mod code;
mod data_handler;
pub mod traits;

use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H256, U256};

use crate::receipt::Receipt;

use self::{
    cache::Cache,
    data_handler::BlockchainDataHandler,
    traits::{
        BlockStorage, FlushableStorage, PersistentStateError, ReceiptStorage, Rollback,
        TransactionStorage,
    },
};

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
            blockchain_data_handler: BlockchainDataHandler::new(),
        }
    }
}

impl BlockStorage for Storage {
    fn get_block_by_number(&self, number: &U256) -> Option<BlockAny> {
        self.cache.get_block_by_number(number).or_else(|| {
            let block = self.blockchain_data_handler.get_block_by_number(number);
            if let Some(ref block) = block {
                self.cache.put_block(block);
            }
            block
        })
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Option<BlockAny> {
        self.cache.get_block_by_hash(block_hash).or_else(|| {
            let block = self.blockchain_data_handler.get_block_by_hash(block_hash);
            if let Some(ref block) = block {
                self.cache.put_block(block);
            }
            block
        })
    }

    fn put_block(&self, block: &BlockAny) {
        self.cache.put_block(block);
        self.blockchain_data_handler.put_block(block);
    }

    fn get_latest_block(&self) -> Option<BlockAny> {
        self.cache.get_latest_block().or_else(|| {
            let latest_block = self.blockchain_data_handler.get_latest_block();
            if let Some(ref block) = latest_block {
                self.cache.put_latest_block(Some(block));
            }
            latest_block
        })
    }

    fn put_latest_block(&self, block: Option<&BlockAny>) {
        self.cache.put_latest_block(block);
        self.blockchain_data_handler.put_latest_block(block);
    }
}

impl TransactionStorage for Storage {
    fn extend_transactions_from_block(&self, block: &BlockAny) {
        // Feature flag
        self.cache.extend_transactions_from_block(block);

        self.blockchain_data_handler
            .extend_transactions_from_block(block);
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Option<TransactionV2> {
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
    ) -> Option<TransactionV2> {
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
    ) -> Option<TransactionV2> {
        self.cache
            .get_transaction_by_block_number_and_index(number, index)
            .or_else(|| {
                let transaction = self
                    .blockchain_data_handler
                    .get_transaction_by_block_number_and_index(number, index);
                if let Some(ref transaction) = transaction {
                    self.cache.put_transaction(transaction)
                }
                transaction
            })
    }

    fn put_transaction(&self, transaction: &TransactionV2) {
        self.cache.put_transaction(transaction);
        self.blockchain_data_handler.put_transaction(transaction);
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

impl FlushableStorage for Storage {
    fn flush(&self) -> Result<(), PersistentStateError> {
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
    fn disconnect_latest_block(&self) {
        self.cache.disconnect_latest_block();
        self.blockchain_data_handler.disconnect_latest_block();
    }
}
