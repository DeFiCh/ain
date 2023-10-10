use std::{collections::HashMap, sync::Arc};

use ethereum::{Block, ReceiptV3, TransactionV2};
use ethereum_types::{Bloom, H160, H256, U256};
use parking_lot::{Mutex, RwLock};
use rand::Rng;

use crate::{
    backend::Vicinity,
    core::XHash,
    receipt::Receipt,
    transaction::{system::SystemTx, SignedTx},
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
    /// Nonces for each account's transactions_queue must be in strictly increasing order. This means that if the last
    /// queued transaction for an account has nonce 3, the next one should have nonce 4. If a `SignedTx` with a
    /// nonce that is not one more than the previous nonce is added, an error is returned. This helps to ensure
    /// the integrity of the block template and enforce correct nonce usage.
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
    pub unsafe fn push_in(
        &self,
        template_id: u64,
        tx: QueueTx,
        hash: XHash,
        gas_used: U256,
        state_root: H256,
    ) -> Result<()> {
        self.with_block_template(template_id, |template| {
            template.queue_tx(tx, hash, gas_used, state_root)
        })
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
    pub unsafe fn get_txs_cloned_in(&self, template_id: u64) -> Result<Vec<QueueTxItem>> {
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
pub enum QueueTx {
    SignedTx(Box<SignedTx>),
    SystemTx(SystemTx),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct QueueTxItem {
    pub tx: QueueTx,
    pub tx_hash: XHash,
    pub gas_used: U256,
    pub state_root: H256,
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
    pub transactions_queue: Vec<QueueTxItem>,
    pub block_data: Option<BlockData>,
    pub all_transactions: Vec<Box<SignedTx>>,
    pub receipts_v3: Vec<ReceiptAndOptionalContractAddress>,
    pub logs_bloom: Bloom,
    pub total_gas_used: U256,
    pub total_gas_fees: U256,
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
            all_transactions: Vec::new(),
            receipts_v3: Vec::new(),
            logs_bloom: Bloom::default(),
            total_gas_used: U256::zero(),
            total_gas_fees: U256::zero(),
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

    pub fn queue_tx(
        &self,
        tx: QueueTx,
        tx_hash: XHash,
        gas_used: U256,
        state_root: H256,
    ) -> Result<()> {
        let mut data = self.data.lock();

        data.total_gas_used += gas_used;

        data.transactions_queue.push(QueueTxItem {
            tx,
            tx_hash,
            gas_used,
            state_root,
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

    pub fn get_queue_txs_cloned(&self) -> Vec<QueueTxItem> {
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

    pub fn is_queued(&self, tx: &QueueTx) -> bool {
        self.data
            .lock()
            .transactions_queue
            .iter()
            .any(|queued| &queued.tx == tx)
    }
}

impl From<SignedTx> for QueueTx {
    fn from(tx: SignedTx) -> Self {
        Self::SignedTx(Box::new(tx))
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

#[cfg(test_off)]
mod tests {
    use std::str::FromStr;

    use ethereum_types::{H256, U256};

    use super::*;
    use crate::transaction::bridge::BalanceUpdate;

    #[test]
    fn test_valid_nonce_order() -> Result<(), BlockTemplateError> {
        let template = BlockTemplate::new();

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        // Nonce 1, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx2 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869018502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0dd1fad9a8465969354d567e8a74af3f6de3e56abbe1b71154d7929d0bd5cc170a0353190adb50e3cfae82a77c2ea56b49a86f72bd0071a70d1c25c49827654aa68").unwrap()));

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx3 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx4 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        template.queue_tx(
            tx1,
            H256::from_low_u64_be(1).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.queue_tx(
            tx2,
            H256::from_low_u64_be(2).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.queue_tx(
            tx3,
            H256::from_low_u64_be(3).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.queue_tx(
            tx4,
            H256::from_low_u64_be(4).into(),
            U256::zero(),
            U256::zero(),
        )?;
        Ok(())
    }
}
