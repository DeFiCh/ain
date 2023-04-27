mod cache;
mod data_handler;
pub mod traits;

use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H256, U256};

use crate::receipt::Receipt;

use self::{
    cache::Cache,
    data_handler::BlockchainDataHandler,
    traits::{
        BlockStorage, FlushableStorage, PersistentStateError, ReceiptStorage, TransactionStorage,
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
        self.cache
            .get_block_by_number(number)
            .or_else(|| self.blockchain_data_handler.get_block_by_number(number))
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Option<BlockAny> {
        self.cache
            .get_block_by_hash(block_hash)
            .or_else(|| self.blockchain_data_handler.get_block_by_hash(block_hash))
    }

    fn put_block(&self, block: BlockAny) {
        self.cache.put_block(block.clone());
        self.blockchain_data_handler.put_block(block)
    }

    fn get_latest_block(&self) -> Option<BlockAny> {
        self.cache
            .get_latest_block()
            .or_else(|| self.blockchain_data_handler.get_latest_block())
    }

    fn put_latest_block(&self, block: BlockAny) {
        self.cache.put_latest_block(block.clone());
        self.blockchain_data_handler.put_latest_block(block)
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
        self.cache
            .get_transaction_by_hash(hash)
            .or_else(|| self.blockchain_data_handler.get_transaction_by_hash(hash))
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: &H256,
        index: usize,
    ) -> Option<TransactionV2> {
        self.cache
            .get_transaction_by_block_hash_and_index(hash, index)
            .or_else(|| {
                self.blockchain_data_handler
                    .get_transaction_by_block_hash_and_index(hash, index)
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
                self.blockchain_data_handler
                    .get_transaction_by_block_number_and_index(number, index)
            })
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
