use std::{collections::HashMap, sync::Arc};

use ethereum::{Block, ReceiptV3, TransactionV2};
use ethereum_types::{Bloom, H160, H256, U256};
use parking_lot::{Mutex, RwLock};
use rand::Rng;

use crate::{
    backend::Vicinity, core::XHash, evm::TxState, receipt::Receipt, transaction::SignedTx,
};

type Result<T> = std::result::Result<T, BlockTemplateError>;

pub type ReceiptAndOptionalContractAddress = (ReceiptV3, Option<H160>);

#[derive(Debug)]
pub struct BlockTemplateMap {
    templates: RwLock<HashMap<u64, Arc<BlockTemplate>>>,
}

impl Default for BlockTemplateMap {
    fn default() -> Self {
        Self::new()
    }
}

/// Holds multiple `BlockTemplate`s, each associated with a unique template ID.
///
/// Template IDs are randomly generated and used to access distinct transaction templates.
impl BlockTemplateMap {
    pub fn new() -> Self {
        BlockTemplateMap {
            templates: RwLock::new(HashMap::new()),
        }
    }

    /// `create` generates a unique random ID, creates a new `BlockTemplate` for that ID,
    /// and then returns the ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn create(
        &self,
        target_block: U256,
        dvm_block: u64,
        state_root: H256,
        beneficiary: H160,
        timestamp: u64,
        block_gas_limit: u64,
    ) -> u64 {
        let mut rng = rand::thread_rng();
        loop {
            let template_id = rng.gen();
            // Safety check to disallow 0 as it's equivalent to no template_id
            if template_id == 0 {
                continue;
            };
            let mut write_guard = self.templates.write();

            if let std::collections::hash_map::Entry::Vacant(e) = write_guard.entry(template_id) {
                e.insert(Arc::new(BlockTemplate::new(
                    target_block,
                    dvm_block,
                    state_root,
                    beneficiary,
                    timestamp,
                    block_gas_limit,
                )));
                return template_id;
            }
        }
    }

    /// Try to remove and return the `BlockTemplate` associated with the provided
    /// template ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove(&self, template_id: u64) -> Option<Arc<BlockTemplate>> {
        self.templates.write().remove(&template_id)
    }

    /// Returns an atomic reference counting pointer of the `BlockTemplate` associated with the provided template ID.
    /// Note that the `BlockTemplate` instance contains the mutex of the `BlockTemplateData`, and this method
    /// should be used if multiple read/write operations on the block template is required within the pipeline. This is
    /// to ensure the atomicity and functionality of the client, and to maintain the integrity of the block template.
    ///
    /// # Errors
    ///
    /// Returns `BlockTemplateError::NoSuchTemplate` if no block template is associated with the given template ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get(&self, template_id: u64) -> Result<Arc<BlockTemplate>> {
        Ok(Arc::clone(
            self.templates
                .read()
                .get(&template_id)
                .ok_or(BlockTemplateError::NoSuchTemplate)?,
        ))
    }

    /// Attempts to add a new transaction to the `BlockTemplate` associated with the provided template ID.
    /// Nonces for each account must be in strictly increasing order. This means that if the last added transaction
    /// for an account has nonce 3, the next one should have nonce 4. If a `SignedTx` with a nonce that is not one
    /// more than the previous nonce is added, an error is returned. This helps to ensure the integrity of the block
    /// template and enforce correct nonce usage.
    ///
    /// # Errors
    ///
    /// Returns `BlockTemplateError::NoSuchTemplate` if no block template is associated with the given template ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn push_in(&self, template_id: u64, tx_update: TxState, hash: XHash) -> Result<()> {
        self.with_block_template(template_id, |template| template.queue_tx(tx_update, hash))
            .and_then(|res| res)
    }

    /// Removes all transactions_queue in the queue whose sender matches the provided sender address.
    /// # Errors
    ///
    /// Returns `BlockTemplateError::NoSuchTemplate` if no template is associated with the given template ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_txs_above_hash_in(
        &self,
        template_id: u64,
        target_hash: XHash,
    ) -> Result<Vec<XHash>> {
        self.with_block_template(template_id, |template| {
            template.remove_txs_above_hash(target_hash)
        })
        .and_then(|res| res)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_txs_cloned_in(&self, template_id: u64) -> Result<Vec<TemplateTxItem>> {
        self.with_block_template(template_id, BlockTemplate::get_queue_txs_cloned)
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_total_gas_used_in(&self, template_id: u64) -> Result<U256> {
        self.with_block_template(template_id, BlockTemplate::get_total_gas_used)
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_target_block_in(&self, template_id: u64) -> Result<U256> {
        self.with_block_template(template_id, BlockTemplate::get_target_block)
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_timestamp_in(&self, template_id: u64) -> Result<u64> {
        self.with_block_template(template_id, BlockTemplate::get_timestamp)
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_latest_state_root_in(&self, template_id: u64) -> Result<H256> {
        self.with_block_template(template_id, BlockTemplate::get_latest_state_root)
    }

    /// Apply the closure to the template associated with the template ID.
    /// # Errors
    ///
    /// Returns `BlockTemplateError::NoSuchTemplate` if no block template is associated with the given template ID.
    unsafe fn with_block_template<T, F>(&self, template_id: u64, f: F) -> Result<T>
    where
        F: FnOnce(&BlockTemplate) -> T,
    {
        match self.templates.read().get(&template_id) {
            Some(template) => Ok(f(template)),
            None => Err(BlockTemplateError::NoSuchTemplate),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TemplateTxItem {
    pub tx: Box<SignedTx>,
    pub tx_hash: XHash,
    pub gas_used: U256,
    pub gas_fees: U256,
    pub state_root: H256,
    pub logs_bloom: Bloom,
    pub receipt_v3: ReceiptAndOptionalContractAddress,
}

impl TemplateTxItem {
    pub fn new_system_tx(
        tx: Box<SignedTx>,
        receipt_v3: ReceiptAndOptionalContractAddress,
        state_root: H256,
        logs_bloom: Bloom,
    ) -> Self {
        TemplateTxItem {
            tx,
            tx_hash: Default::default(),
            gas_used: U256::zero(),
            gas_fees: U256::zero(),
            state_root,
            logs_bloom,
            receipt_v3,
        }
    }
}

#[derive(Clone, Debug)]
pub struct BlockData {
    pub block: Block<TransactionV2>,
    pub receipts: Vec<Receipt>,
}

/// The `BlockTemplateData` contains:
/// 1. Queue of validated transactions_queue
/// 2. Block data
/// 3. All transactions_queue added into the block template
/// 4. All transaction receipts added into the block template
/// 5. Logs bloom
/// 6. Total gas used by all transactions_queue in the block
/// 7. Total gas fees of all transactions_queue in the block
/// 8. Backend vicinity
/// 9. DVM block number
/// 10. Initial state root
///
/// The template is used to construct a valid EVM block.
///
#[derive(Clone, Debug, Default)]
pub struct BlockTemplateData {
    pub transactions_queue: Vec<TemplateTxItem>,
    pub block_data: Option<BlockData>,
    pub total_gas_used: U256,
    pub vicinity: Vicinity,
    pub timestamp: u64,
    pub dvm_block: u64,
    pub initial_state_root: H256,
}

impl BlockTemplateData {
    pub fn new(
        target_block: U256,
        dvm_block: u64,
        state_root: H256,
        beneficiary: H160,
        timestamp: u64,
        block_gas_limit: u64,
    ) -> Self {
        Self {
            transactions_queue: Vec::new(),
            block_data: None,
            total_gas_used: U256::zero(),
            vicinity: Vicinity {
                beneficiary,
                timestamp: U256::from(timestamp),
                block_number: target_block,
                block_gas_limit: U256::from(block_gas_limit),
                ..Vicinity::default()
            },
            timestamp,
            dvm_block,
            initial_state_root: state_root,
        }
    }
}

#[derive(Debug, Default)]
pub struct BlockTemplate {
    pub data: Mutex<BlockTemplateData>,
}

impl BlockTemplate {
    fn new(
        target_block: U256,
        dvm_block: u64,
        state_root: H256,
        beneficiary: H160,
        timestamp: u64,
        block_gas_limit: u64,
    ) -> Self {
        Self {
            data: Mutex::new(BlockTemplateData::new(
                target_block,
                dvm_block,
                state_root,
                beneficiary,
                timestamp,
                block_gas_limit,
            )),
        }
    }

    pub fn queue_tx(&self, tx_update: TxState, tx_hash: XHash) -> Result<()> {
        let mut data = self.data.lock();

        data.total_gas_used += tx_update.gas_used;

        data.transactions_queue.push(TemplateTxItem {
            tx: tx_update.tx,
            tx_hash,
            gas_used: tx_update.gas_used,
            gas_fees: tx_update.gas_fees,
            state_root: tx_update.state_root,
            logs_bloom: tx_update.logs_bloom,
            receipt_v3: tx_update.receipt,
        });
        Ok(())
    }

    pub fn remove_txs_above_hash(&self, target_hash: XHash) -> Result<Vec<XHash>> {
        let mut data = self.data.lock();
        let mut removed_txs = Vec::new();

        if let Some(index) = data
            .transactions_queue
            .iter()
            .position(|item| item.tx_hash == target_hash)
        {
            removed_txs = data
                .transactions_queue
                .drain(index..)
                .map(|tx_item| tx_item.tx_hash)
                .collect();

            data.total_gas_used = data
                .transactions_queue
                .iter()
                .fold(U256::zero(), |acc, tx| acc + tx.gas_used)
        }

        Ok(removed_txs)
    }

    pub fn get_queue_txs_cloned(&self) -> Vec<TemplateTxItem> {
        self.data.lock().transactions_queue.clone()
    }

    pub fn get_total_gas_used(&self) -> U256 {
        self.data.lock().total_gas_used
    }

    pub fn get_target_block(&self) -> U256 {
        self.data.lock().vicinity.block_number
    }

    pub fn get_timestamp(&self) -> u64 {
        self.data.lock().timestamp
    }

    pub fn get_state_root_from_native_hash(&self, hash: XHash) -> Option<H256> {
        self.data
            .lock()
            .transactions_queue
            .iter()
            .find(|tx_item| tx_item.tx_hash == hash)
            .map(|tx_item| tx_item.state_root)
    }

    pub fn get_latest_state_root(&self) -> H256 {
        let data = self.data.lock();
        data.transactions_queue
            .last()
            .map_or(data.initial_state_root, |tx_item| tx_item.state_root)
    }

    pub fn get_latest_logs_bloom(&self) -> Bloom {
        let data = self.data.lock();
        data.transactions_queue
            .last()
            .map_or(Bloom::default(), |tx_item| tx_item.logs_bloom)
    }

    pub fn get_block_number(&self) -> U256 {
        let data = self.data.lock();
        data.vicinity.block_number
    }
}

#[derive(Debug)]
pub enum BlockTemplateError {
    NoSuchTemplate,
}

impl std::fmt::Display for BlockTemplateError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            BlockTemplateError::NoSuchTemplate => write!(f, "No block template for this id"),
        }
    }
}

impl std::error::Error for BlockTemplateError {}
