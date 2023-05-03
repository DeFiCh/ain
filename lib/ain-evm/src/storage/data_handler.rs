use std::{collections::HashMap, sync::RwLock};

use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H256, U256};
use std::borrow::ToOwned;

use crate::receipt::Receipt;

use super::traits::{
    BlockStorage, FlushableStorage, PersistentState, PersistentStateError, ReceiptStorage,
    TransactionStorage,
};

pub static BLOCK_MAP_PATH: &str = "block_map.bin";
pub static BLOCK_DATA_PATH: &str = "block_data.bin";
pub static LATEST_BLOCK_DATA_PATH: &str = "latest_block_data.bin";
pub static RECEIPT_MAP_PATH: &str = "receipt_map.bin";
pub static CODE_MAP_PATH: &str = "code_map.bin";
// pub static TRANSACTION_DATA_PATH: &str = "transaction_data.bin";

type BlockHashtoBlock = HashMap<H256, U256>;
type Blocks = HashMap<U256, BlockAny>;
type TxHashToTx = HashMap<H256, TransactionV2>;
type LatestBlockNumber = U256;
type TransactionHashToReceipt = HashMap<H256, Receipt>;
type CodeHashToCode = HashMap<H256, Vec<u8>>;

impl PersistentState for BlockHashtoBlock {}
impl PersistentState for Blocks {}
impl PersistentState for LatestBlockNumber {}
impl PersistentState for TransactionHashToReceipt {}
impl PersistentState for CodeHashToCode {}

#[derive(Debug)]
pub struct BlockchainDataHandler {
    // Improvements: Add transaction_map behind feature flag -txindex or equivalent
    transactions: RwLock<TxHashToTx>,

    receipts: RwLock<TransactionHashToReceipt>,

    block_map: RwLock<BlockHashtoBlock>,
    blocks: RwLock<Blocks>,
    latest_block_number: RwLock<Option<LatestBlockNumber>>,

    code_map: RwLock<CodeHashToCode>,
}

impl BlockchainDataHandler {
    pub fn new() -> Self {
        let blocks = Blocks::load_from_disk(BLOCK_DATA_PATH).expect("Error loading blocks data");
        BlockchainDataHandler {
            transactions: RwLock::new(HashMap::new()),
            block_map: RwLock::new(
                BlockHashtoBlock::load_from_disk(BLOCK_MAP_PATH)
                    .expect("Error loading block_map data"),
            ),
            latest_block_number: RwLock::new(
                LatestBlockNumber::load_from_disk(LATEST_BLOCK_DATA_PATH).ok(),
            ),
            blocks: RwLock::new(blocks),
            receipts: RwLock::new(
                TransactionHashToReceipt::load_from_disk(RECEIPT_MAP_PATH)
                    .expect("Error loading receipts data"),
            ),
            code_map: RwLock::new(
                CodeHashToCode::load_from_disk(CODE_MAP_PATH).expect("Error loading code data"),
            ),
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

    fn get_transaction_by_hash(&self, _hash: &H256) -> Option<TransactionV2> {
        // TODO: Feature flag
        None // Unimplement without tx index
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

    fn put_latest_block(&self, block: &BlockAny) {
        let mut latest_block_number = self.latest_block_number.write().unwrap();
        *latest_block_number = Some(block.header.number);
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

    pub fn put_code(&self, hash: &H256, code: &Vec<u8>) -> Option<Vec<u8>> {
        self.code_map
            .write()
            .unwrap()
            .insert(hash.clone(), code.clone())
    }
}
