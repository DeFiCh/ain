use std::borrow::ToOwned;
use std::{collections::HashMap, sync::RwLock};

use ain_cpp_imports::Attributes;
use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H160, H256, U256};

use super::traits::AttributesStorage;
use super::{
    code::CodeHistory,
    traits::{
        BlockStorage, FlushableStorage, PersistentState, ReceiptStorage, Rollback,
        TransactionStorage,
    },
};
use crate::log::LogIndex;
use crate::receipt::Receipt;
use crate::storage::traits::LogStorage;
use crate::Result;

pub static BLOCK_MAP_PATH: &str = "block_map.bin";
pub static BLOCK_DATA_PATH: &str = "block_data.bin";
pub static LATEST_BLOCK_DATA_PATH: &str = "latest_block_data.bin";
pub static RECEIPT_MAP_PATH: &str = "receipt_map.bin";
pub static CODE_MAP_PATH: &str = "code_map.bin";
pub static TRANSACTION_DATA_PATH: &str = "transaction_data.bin";
pub static ADDRESS_LOGS_MAP_PATH: &str = "address_logs_map.bin";
pub static ATTRIBUTES_DATA_PATH: &str = "attributes_data.bin";

type BlockHashtoBlock = HashMap<H256, U256>;
type Blocks = HashMap<U256, BlockAny>;
type TxHashToTx = HashMap<H256, TransactionV2>;
type LatestBlockNumber = U256;
type TransactionHashToReceipt = HashMap<H256, Receipt>;
type AddressToLogs = HashMap<U256, HashMap<H160, Vec<LogIndex>>>;
type OptionalAttributes = Option<Attributes>;

impl PersistentState for BlockHashtoBlock {}
impl PersistentState for Blocks {}
impl PersistentState for LatestBlockNumber {}
impl PersistentState for TransactionHashToReceipt {}
impl PersistentState for TxHashToTx {}
impl PersistentState for AddressToLogs {}
impl PersistentState for OptionalAttributes {}

#[derive(Debug, Default)]
pub struct BlockchainDataHandler {
    // Improvements: Add transaction_map behind feature flag -txindex or equivalent
    transactions: RwLock<TxHashToTx>,

    receipts: RwLock<TransactionHashToReceipt>,

    block_map: RwLock<BlockHashtoBlock>,
    blocks: RwLock<Blocks>,
    latest_block_number: RwLock<Option<LatestBlockNumber>>,

    code_map: RwLock<CodeHistory>,

    address_logs_map: RwLock<AddressToLogs>,
    attributes: RwLock<Option<Attributes>>,
}

impl BlockchainDataHandler {
    pub fn restore() -> Self {
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
            code_map: RwLock::new(CodeHistory::load_from_disk(CODE_MAP_PATH).unwrap_or_default()),
            address_logs_map: RwLock::new(
                AddressToLogs::load_from_disk(ADDRESS_LOGS_MAP_PATH).unwrap_or_default(),
            ),
            attributes: RwLock::new(
                OptionalAttributes::load_from_disk(ATTRIBUTES_DATA_PATH)
                    .expect("Error loading attributes data"),
            ),
        }
    }
}

impl TransactionStorage for BlockchainDataHandler {
    // TODO: Feature flag
    fn extend_transactions_from_block(&self, block: &BlockAny) -> Result<()> {
        let mut transactions = self.transactions.write().unwrap();

        for transaction in &block.transactions {
            let hash = transaction.hash();
            transactions.insert(hash, transaction.clone());
        }
        Ok(())
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Result<Option<TransactionV2>> {
        let transaction = self
            .transactions
            .read()
            .unwrap()
            .get(hash)
            .map(ToOwned::to_owned);
        Ok(transaction)
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        block_hash: &H256,
        index: usize,
    ) -> Result<Option<TransactionV2>> {
        self.block_map
            .write()
            .unwrap()
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
            .write()
            .unwrap()
            .get(block_number)
            .and_then(|block| block.transactions.get(index).map(ToOwned::to_owned));
        Ok(transaction)
    }

    fn put_transaction(&self, transaction: &TransactionV2) -> Result<()> {
        self.transactions
            .write()
            .unwrap()
            .insert(transaction.hash(), transaction.clone());
        Ok(())
    }
}

impl BlockStorage for BlockchainDataHandler {
    fn get_block_by_number(&self, number: &U256) -> Result<Option<BlockAny>> {
        let block = self
            .blocks
            .write()
            .unwrap()
            .get(number)
            .map(ToOwned::to_owned);
        Ok(block)
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Result<Option<BlockAny>> {
        self.block_map
            .write()
            .unwrap()
            .get(block_hash)
            .map_or(Ok(None), |block_number| {
                self.get_block_by_number(block_number)
            })
    }

    fn put_block(&self, block: &BlockAny) -> Result<()> {
        self.extend_transactions_from_block(block)?;

        let block_number = block.header.number;
        let hash = block.header.hash();
        self.blocks
            .write()
            .unwrap()
            .insert(block_number, block.clone());
        self.block_map.write().unwrap().insert(hash, block_number);
        Ok(())
    }

    fn get_latest_block(&self) -> Result<Option<BlockAny>> {
        self.latest_block_number
            .read()
            .unwrap()
            .as_ref()
            .map_or(Ok(None), |number| self.get_block_by_number(number))
    }

    fn put_latest_block(&self, block: Option<&BlockAny>) -> Result<()> {
        let mut latest_block_number = self.latest_block_number.write().unwrap();
        *latest_block_number = block.map(|b| b.header.number);
        Ok(())
    }
}

impl ReceiptStorage for BlockchainDataHandler {
    fn get_receipt(&self, tx: &H256) -> Result<Option<Receipt>> {
        let receipt = self.receipts.read().unwrap().get(tx).map(ToOwned::to_owned);
        Ok(receipt)
    }

    fn put_receipts(&self, receipts: Vec<Receipt>) -> Result<()> {
        let mut receipt_map = self.receipts.write().unwrap();
        for receipt in receipts {
            receipt_map.insert(receipt.tx_hash, receipt);
        }
        Ok(())
    }
}

impl LogStorage for BlockchainDataHandler {
    fn get_logs(&self, block_number: &U256) -> Result<Option<HashMap<H160, Vec<LogIndex>>>> {
        let logs = self
            .address_logs_map
            .read()
            .unwrap()
            .get(block_number)
            .map(ToOwned::to_owned);
        Ok(logs)
    }

    fn put_logs(&self, address: H160, logs: Vec<LogIndex>, block_number: U256) -> Result<()> {
        let mut address_logs_map = self.address_logs_map.write().unwrap();

        let address_map = address_logs_map.entry(block_number).or_default();
        address_map.insert(address, logs);
        Ok(())
    }
}

impl FlushableStorage for BlockchainDataHandler {
    fn flush(&self) -> Result<()> {
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
        self.code_map.write().unwrap().save_to_disk(CODE_MAP_PATH)?;
        self.address_logs_map
            .write()
            .unwrap()
            .save_to_disk(ADDRESS_LOGS_MAP_PATH)
    }
}

impl BlockchainDataHandler {
    pub fn get_code_by_hash(&self, hash: &H256) -> Result<Option<Vec<u8>>> {
        let code = self
            .code_map
            .read()
            .unwrap()
            .get(hash)
            .map(ToOwned::to_owned);
        Ok(code)
    }

    pub fn put_code(&self, hash: &H256, code: &[u8]) -> Result<()> {
        let block_number = self
            .get_latest_block()?
            .map(|b| b.header.number)
            .unwrap_or_default()
            + 1;
        self.code_map
            .write()
            .unwrap()
            .insert(block_number, *hash, code.to_vec());
        Ok(())
    }
}

impl Rollback for BlockchainDataHandler {
    fn disconnect_latest_block(&self) -> Result<()> {
        if let Some(block) = self.get_latest_block()? {
            println!("disconnecting block number : {:x?}", block.header.number);
            let mut transactions = self.transactions.write().unwrap();
            let mut receipts = self.receipts.write().unwrap();
            for tx in &block.transactions {
                let hash = &tx.hash();
                transactions.remove(hash);
                receipts.remove(hash);
            }

            self.block_map.write().unwrap().remove(&block.header.hash());
            self.blocks.write().unwrap().remove(&block.header.number);
            self.code_map.write().unwrap().rollback(block.header.number);

            self.put_latest_block(self.get_block_by_hash(&block.header.parent_hash)?.as_ref())?
        }
        Ok(())
    }
}

impl AttributesStorage for BlockchainDataHandler {
    fn put_attributes(&self, attr: Option<&Attributes>) -> Result<()> {
        let mut attributes = self.attributes.write().unwrap();
        *attributes = attr.cloned();
        Ok(())
    }

    fn get_attributes(&self) -> Result<Option<Attributes>> {
        let attributes = self.attributes.read().unwrap().as_ref().cloned();
        Ok(attributes)
    }
}
