use std::{collections::HashMap, sync::Arc};

use parking_lot::{Mutex, RwLock};

use ethereum::{Block, TransactionV2};
use ethereum_types::{H256, U256};
use rand::Rng;

use crate::{
    core::XHash,
    receipt::Receipt,
    transaction::{system::SystemTx, SignedTx},
};

type Result<T> = std::result::Result<T, QueueError>;

#[derive(Debug)]
pub struct TransactionQueueMap {
    queues: RwLock<HashMap<u64, Arc<TransactionQueue>>>,
}

impl Default for TransactionQueueMap {
    fn default() -> Self {
        Self::new()
    }
}

/// Holds multiple `TransactionQueue`s, each associated with a unique queue ID.
///
/// Queue IDs are randomly generated and used to access distinct transaction queues.
impl TransactionQueueMap {
    pub fn new() -> Self {
        TransactionQueueMap {
            queues: RwLock::new(HashMap::new()),
        }
    }

    /// `get_queue_id` generates a unique random ID, creates a new `TransactionQueue` for that ID,
    /// and then returns the ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn create(&self, target_block: U256, state_root: H256) -> u64 {
        let mut rng = rand::thread_rng();
        loop {
            let queue_id = rng.gen();
            // Safety check to disallow 0 as it's equivalent to no queue_id
            if queue_id == 0 {
                continue;
            };
            let mut write_guard = self.queues.write();

            if let std::collections::hash_map::Entry::Vacant(e) = write_guard.entry(queue_id) {
                e.insert(Arc::new(TransactionQueue::new(target_block, state_root)));
                return queue_id;
            }
        }
    }

    /// Try to remove and return the `TransactionQueue` associated with the provided
    /// queue ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove(&self, queue_id: u64) -> Option<Arc<TransactionQueue>> {
        self.queues.write().remove(&queue_id)
    }

    /// Returns an atomic reference counting pointer of the `TransactionQueue` associated with the provided queue ID.
    /// Note that the `TransactionQueue` instance contains the mutex of the `TransactionQueueData`, and this method
    /// should be used if multiple read/write operations on the tx queue is required within the pipeline. This is to
    /// ensure the atomicity and functionality of the client, and to maintain the integrity of the transaction queue.
    ///
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get(&self, queue_id: u64) -> Result<Arc<TransactionQueue>> {
        Ok(Arc::clone(
            self.queues
                .read()
                .get(&queue_id)
                .ok_or(QueueError::NoSuchQueue)?,
        ))
    }

    /// Attempts to add a new transaction to the `TransactionQueue` associated with the provided queue ID. If the
    /// transaction is a `SignedTx`, it also updates the corresponding account's nonce.
    /// Nonces for each account's transactions must be in strictly increasing order. This means that if the last
    /// queued transaction for an account has nonce 3, the next one should have nonce 4. If a `SignedTx` with a
    /// nonce that is not one more than the previous nonce is added, an error is returned. This helps to ensure
    /// the integrity of the transaction queue and enforce correct nonce usage.
    ///
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    /// Returns `QueueError::InvalidNonce` if a `SignedTx` is provided with a nonce that is not one more than the
    /// previous nonce of transactions from the same sender in the queue.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn push_in(
        &self,
        queue_id: u64,
        tx: QueueTx,
        hash: XHash,
        gas_used: U256,
        state_root: H256,
    ) -> Result<()> {
        self.with_transaction_queue(queue_id, |queue| {
            queue.queue_tx(tx, hash, gas_used, state_root)
        })
        .and_then(|res| res)
    }

    /// Removes all transactions in the queue whose sender matches the provided sender address.
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_txs_above_hash_in(
        &self,
        queue_id: u64,
        target_hash: XHash,
    ) -> Result<Vec<XHash>> {
        self.with_transaction_queue(queue_id, |queue| queue.remove_txs_above_hash(target_hash))
            .and_then(|res| res)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_txs_cloned_in(&self, queue_id: u64) -> Result<Vec<QueueTxItem>> {
        self.with_transaction_queue(queue_id, TransactionQueue::get_queue_txs_cloned)
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_total_gas_used_in(&self, queue_id: u64) -> Result<U256> {
        self.with_transaction_queue(queue_id, TransactionQueue::get_total_gas_used)
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_target_block_in(&self, queue_id: u64) -> Result<U256> {
        self.with_transaction_queue(queue_id, TransactionQueue::get_target_block)
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_latest_state_root_in(&self, queue_id: u64) -> Result<H256> {
        self.with_transaction_queue(queue_id, TransactionQueue::get_latest_state_root)
    }

    /// Apply the closure to the queue associated with the queue ID.
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    unsafe fn with_transaction_queue<T, F>(&self, queue_id: u64, f: F) -> Result<T>
    where
        F: FnOnce(&TransactionQueue) -> T,
    {
        match self.queues.read().get(&queue_id) {
            Some(queue) => Ok(f(queue)),
            None => Err(QueueError::NoSuchQueue),
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

/// The `TransactionQueueData` contains:
/// 1. Queue of validated transactions
/// 2. Map of the account nonces
/// 3. Block data
/// 4. Total gas used by all queued transactions
///
/// It's used to manage and process transactions for different accounts.
///
#[derive(Clone, Debug, Default)]
pub struct TransactionQueueData {
    pub transactions: Vec<QueueTxItem>,
    pub block_data: Option<BlockData>,
    pub total_gas_used: U256,
    pub target_block: U256,
    pub initial_state_root: H256,
}

impl TransactionQueueData {
    pub fn new(target_block: U256, state_root: H256) -> Self {
        Self {
            transactions: Vec::new(),
            total_gas_used: U256::zero(),
            block_data: None,
            target_block,
            initial_state_root: state_root,
        }
    }
}

#[derive(Debug, Default)]
pub struct TransactionQueue {
    pub data: Mutex<TransactionQueueData>,
}

impl TransactionQueue {
    fn new(target_block: U256, state_root: H256) -> Self {
        Self {
            data: Mutex::new(TransactionQueueData::new(target_block, state_root)),
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

        data.transactions.push(QueueTxItem {
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
            .transactions
            .iter()
            .position(|item| item.tx_hash == target_hash)
        {
            removed_txs = data
                .transactions
                .drain(index..)
                .map(|tx_item| tx_item.tx_hash)
                .collect();

            data.total_gas_used = data
                .transactions
                .iter()
                .fold(U256::zero(), |acc, tx| acc + tx.gas_used)
        }

        Ok(removed_txs)
    }

    pub fn get_queue_txs_cloned(&self) -> Vec<QueueTxItem> {
        self.data.lock().transactions.clone()
    }

    pub fn get_total_gas_used(&self) -> U256 {
        self.data.lock().total_gas_used
    }

    pub fn get_target_block(&self) -> U256 {
        self.data.lock().target_block
    }

    pub fn get_state_root_from_native_hash(&self, hash: XHash) -> Option<H256> {
        self.data
            .lock()
            .transactions
            .iter()
            .find(|tx_item| tx_item.tx_hash == hash)
            .map(|tx_item| tx_item.state_root)
    }

    pub fn get_latest_state_root(&self) -> H256 {
        let data = self.data.lock();
        data.transactions
            .last()
            .map_or(data.initial_state_root, |tx_item| tx_item.state_root)
    }

    pub fn is_queued(&self, tx: &QueueTx) -> bool {
        self.data
            .lock()
            .transactions
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
pub enum QueueError {
    NoSuchQueue,
    InvalidNonce((Box<SignedTx>, U256)),
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            QueueError::NoSuchQueue => write!(f, "No transaction queue for this queue"),
            QueueError::InvalidNonce((tx, nonce)) => write!(f, "Invalid nonce {:x?} for tx {:x?}. Previous queued nonce is {}. TXs should be queued in increasing nonce order.", tx.nonce(), tx.transaction.hash(), nonce),
        }
    }
}

impl std::error::Error for QueueError {}

#[cfg(test_off)]
mod tests {
    use std::str::FromStr;

    use ethereum_types::{H256, U256};

    use super::*;
    use crate::transaction::bridge::BalanceUpdate;

    #[test]
    fn test_invalid_nonce_order() -> Result<(), QueueError> {
        let queue = TransactionQueue::new();

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx2 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx3 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        queue.queue_tx(
            tx1,
            H256::from_low_u64_be(1).into(),
            U256::zero(),
            U256::zero(),
        )?;
        queue.queue_tx(
            tx2,
            H256::from_low_u64_be(2).into(),
            U256::zero(),
            U256::zero(),
        )?;
        // Should fail as nonce 2 is already queued for this sender
        let queued = queue.queue_tx(
            tx3,
            H256::from_low_u64_be(3).into(),
            U256::zero(),
            U256::zero(),
        );
        assert!(matches!(queued, Err(QueueError::InvalidNonce { .. })));
        Ok(())
    }

    #[test]
    fn test_invalid_nonce_order_with_transfer_domain() -> Result<(), QueueError> {
        let queue = TransactionQueue::new();

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx2 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        // sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx3 = QueueTx::BridgeTx(BridgeTx::EvmIn(BalanceUpdate {
            address: H160::from_str("0x6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap(),
            amount: U256::one(),
        }));

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx4 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        queue.queue_tx(
            tx1,
            H256::from_low_u64_be(1).into(),
            U256::zero(),
            U256::zero(),
        )?;
        queue.queue_tx(
            tx2,
            H256::from_low_u64_be(2).into(),
            U256::zero(),
            U256::zero(),
        )?;
        queue.queue_tx(
            tx3,
            H256::from_low_u64_be(3).into(),
            U256::zero(),
            U256::zero(),
        )?;
        // Should fail as nonce 2 is already queued for this sender
        let queued = queue.queue_tx(
            tx4,
            H256::from_low_u64_be(4).into(),
            U256::zero(),
            U256::zero(),
        );
        assert!(matches!(queued, Err(QueueError::InvalidNonce { .. })));
        Ok(())
    }

    #[test]
    fn test_valid_nonce_order() -> Result<(), QueueError> {
        let queue = TransactionQueue::new();

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        // Nonce 1, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx2 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869018502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0dd1fad9a8465969354d567e8a74af3f6de3e56abbe1b71154d7929d0bd5cc170a0353190adb50e3cfae82a77c2ea56b49a86f72bd0071a70d1c25c49827654aa68").unwrap()));

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx3 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx4 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        queue.queue_tx(
            tx1,
            H256::from_low_u64_be(1).into(),
            U256::zero(),
            U256::zero(),
        )?;
        queue.queue_tx(
            tx2,
            H256::from_low_u64_be(2).into(),
            U256::zero(),
            U256::zero(),
        )?;
        queue.queue_tx(
            tx3,
            H256::from_low_u64_be(3).into(),
            U256::zero(),
            U256::zero(),
        )?;
        queue.queue_tx(
            tx4,
            H256::from_low_u64_be(4).into(),
            U256::zero(),
            U256::zero(),
        )?;
        Ok(())
    }
}
