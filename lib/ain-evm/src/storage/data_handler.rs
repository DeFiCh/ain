use std::{collections::HashMap, sync::RwLock};

use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H256, U256};
use std::borrow::ToOwned;

use crate::receipt::Receipt;

use super::{
    code::CodeHistory,
    traits::{
        BlockStorage, FlushableStorage, PersistentState, PersistentStateError, ReceiptStorage,
        Rollback, TransactionStorage,
    },
};

pub static BLOCK_MAP_PATH: &str = "block_map.bin";
pub static BLOCK_DATA_PATH: &str = "block_data.bin";
pub static LATEST_BLOCK_DATA_PATH: &str = "latest_block_data.bin";
pub static RECEIPT_MAP_PATH: &str = "receipt_map.bin";
pub static CODE_MAP_PATH: &str = "code_map.bin";
pub static TRANSACTION_DATA_PATH: &str = "transaction_data.bin";
pub static BASE_FEE_MAP_PATH: &str = "base_fee_map.bin";

type BlockHashtoBlock = HashMap<H256, U256>;
type Blocks = HashMap<U256, BlockAny>;
type TxHashToTx = HashMap<H256, TransactionV2>;
type LatestBlockNumber = U256;
type TransactionHashToReceipt = HashMap<H256, Receipt>;
type BlockHashtoBaseFee = HashMap<H256, U256>;

impl PersistentState for BlockHashtoBlock {}
impl PersistentState for Blocks {}
impl PersistentState for LatestBlockNumber {}
impl PersistentState for TransactionHashToReceipt {}
impl PersistentState for TxHashToTx {}

#[derive(Debug)]
pub struct BlockchainDataHandler {
    // Improvements: Add transaction_map behind feature flag -txindex or equivalent
    transactions: RwLock<TxHashToTx>,

    receipts: RwLock<TransactionHashToReceipt>,

    block_map: RwLock<BlockHashtoBlock>,
    blocks: RwLock<Blocks>,
    latest_block_number: RwLock<Option<LatestBlockNumber>>,
    base_fee_map: RwLock<BlockHashtoBaseFee>,

    code_map: RwLock<CodeHistory>,
}

impl BlockchainDataHandler {
    pub fn new() -> Self {
        BlockchainDataHandler {
            transactions: RwLock::new(
                TxHashToTx::load_from_disk(TRANSACTION_DATA_PATH)
                    .expect("Error loading blocks data"),
            ),
            block_map: RwLock::new(
                BlockHashtoBlock::load_from_disk(BLOCK_MAP_PATH)
                    .expect("Error loading block_map data"),
            ),
            latest_block_number: RwLock::new(
                LatestBlockNumber::load_from_disk(LATEST_BLOCK_DATA_PATH).ok(),
            ),
            blocks: RwLock::new(
                Blocks::load_from_disk(BLOCK_DATA_PATH).expect("Error loading blocks data"),
            ),
            receipts: RwLock::new(
                TransactionHashToReceipt::load_from_disk(RECEIPT_MAP_PATH)
                    .expect("Error loading receipts data"),
            ),
            base_fee_map: RwLock::new(
                BlockHashtoBaseFee::load_from_disk(BASE_FEE_MAP_PATH).unwrap_or_default(),
            ),
            code_map: RwLock::new(CodeHistory::load_from_disk(CODE_MAP_PATH).unwrap_or_default()),
        }
    }
}

impl TransactionStorage for BlockchainDataHandler {
    // TODO: Feature flag
    fn extend_transactions_from_block(&self, block: &BlockAny) {
        let mut transactions = self.transactions.write().unwrap();

        for transaction in &block.transactions {
            let hash = transaction.hash();
            transactions.insert(hash, transaction.clone());
        }
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Option<TransactionV2> {
        self.transactions
            .read()
            .unwrap()
            .get(hash)
            .map(ToOwned::to_owned)
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        block_hash: &H256,
        index: usize,
    ) -> Option<TransactionV2> {
        self.block_map
            .write()
            .unwrap()
            .get(block_hash)
            .and_then(|block_number| {
                self.get_transaction_by_block_number_and_index(block_number, index)
            })
    }

    fn get_transaction_by_block_number_and_index(
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

    fn put_transaction(&self, transaction: &TransactionV2) {
        self.transactions
            .write()
            .unwrap()
            .insert(transaction.hash(), transaction.clone());
    }
}

impl BlockStorage for BlockchainDataHandler {
    fn get_block_by_number(&self, number: &U256) -> Option<BlockAny> {
        self.blocks
            .write()
            .unwrap()
            .get(number)
            .map(ToOwned::to_owned)
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Option<BlockAny> {
        self.block_map
            .write()
            .unwrap()
            .get(block_hash)
            .and_then(|block_number| self.get_block_by_number(block_number))
    }

    fn put_block(&self, block: &BlockAny) {
        self.extend_transactions_from_block(block);

        let block_number = block.header.number;
        let hash = block.header.hash();
        self.blocks
            .write()
            .unwrap()
            .insert(block_number, block.clone());
        self.block_map.write().unwrap().insert(hash, block_number);
    }

    fn get_latest_block(&self) -> Option<BlockAny> {
        self.latest_block_number
            .read()
            .unwrap()
            .as_ref()
            .and_then(|number| self.get_block_by_number(number))
    }

    fn put_latest_block(&self, block: Option<&BlockAny>) {
        let mut latest_block_number = self.latest_block_number.write().unwrap();
        *latest_block_number = block.map(|b| b.header.number);
    }

    fn get_base_fee(&self, block_hash: &H256) -> Option<U256> {
        self.base_fee_map
            .read()
            .unwrap()
            .get(block_hash)
            .map(ToOwned::to_owned)
    }

    fn set_base_fee(&self, block_hash: H256, base_fee: U256) {
        let mut base_fee_map = self.base_fee_map.write().unwrap();
        base_fee_map.insert(block_hash, base_fee);
    }
}

impl ReceiptStorage for BlockchainDataHandler {
    fn get_receipt(&self, tx: &H256) -> Option<Receipt> {
        self.receipts.read().unwrap().get(tx).map(ToOwned::to_owned)
    }

    fn put_receipts(&self, receipts: Vec<Receipt>) {
        let mut receipt_map = self.receipts.write().unwrap();
        for receipt in receipts {
            receipt_map.insert(receipt.tx_hash, receipt);
        }
    }
}

impl FlushableStorage for BlockchainDataHandler {
    fn flush(&self) -> Result<(), PersistentStateError> {
        self.block_map
            .write()
            .unwrap()
            .save_to_disk(BLOCK_MAP_PATH)?;
        self.blocks.write().unwrap().save_to_disk(BLOCK_DATA_PATH)?;
        self.latest_block_number
            .write()
            .unwrap()
            .unwrap_or_default()
            .save_to_disk(LATEST_BLOCK_DATA_PATH)?;
        self.receipts
            .write()
            .unwrap()
            .save_to_disk(RECEIPT_MAP_PATH)?;
        self.transactions
            .write()
            .unwrap()
            .save_to_disk(TRANSACTION_DATA_PATH)?;
        self.code_map.write().unwrap().save_to_disk(CODE_MAP_PATH)
    }
}

impl BlockchainDataHandler {
    pub fn get_code_by_hash(&self, hash: &H256) -> Option<Vec<u8>> {
        self.code_map
            .read()
            .unwrap()
            .get(hash)
            .map(ToOwned::to_owned)
    }

    pub fn put_code(&self, hash: &H256, code: &[u8]) {
        let block_number = self
            .get_latest_block()
            .map(|b| b.header.number)
            .unwrap_or_default()
            + 1;
        self.code_map
            .write()
            .unwrap()
            .insert(block_number, *hash, code.to_vec())
    }
}

impl Rollback for BlockchainDataHandler {
    fn disconnect_latest_block(&self) {
        if let Some(block) = self.get_latest_block() {
            println!("disconnecting block number : {:x?}", block.header.number);
            let mut transactions = self.transactions.write().unwrap();
            let mut receipts = self.receipts.write().unwrap();
            for tx in &block.transactions {
                let hash = &tx.hash();
                transactions.remove(hash);
                receipts.remove(hash);
            }

            self.block_map.write().unwrap().remove(&block.header.hash());
            self.base_fee_map
                .write()
                .unwrap()
                .remove(&block.header.hash());
            self.blocks.write().unwrap().remove(&block.header.number);
            self.code_map.write().unwrap().rollback(block.header.number);

            self.put_latest_block(self.get_block_by_hash(&block.header.parent_hash).as_ref())
        }
    }
}
