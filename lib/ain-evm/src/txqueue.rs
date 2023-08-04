use crate::core::NativeTxHash;
use crate::fee::calculate_gas_fee;
use crate::receipt::Receipt;
use crate::transaction::{system::SystemTx, SignedTx};

use ethereum::{Block, TransactionV2};
use ethereum_types::{H160, U256};
use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Arc, Mutex, RwLock},
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
    pub fn get_queue_id(&self) -> u64 {
        let mut rng = rand::thread_rng();
        loop {
            let queue_id = rng.gen();
            // Safety check to disallow 0 as it's equivalent to no queue_id
            if queue_id == 0 {
                continue;
            };
            let mut write_guard = self.queues.write().unwrap();

            if let std::collections::hash_map::Entry::Vacant(e) = write_guard.entry(queue_id) {
                e.insert(Arc::new(TransactionQueue::new()));
                return queue_id;
            }
        }
    }

    /// Try to remove and return the `TransactionQueue` associated with the provided
    /// queue ID.
    pub fn remove(&self, queue_id: u64) -> Option<Arc<TransactionQueue>> {
        self.queues.write().unwrap().remove(&queue_id)
    }

    /// Clears the `TransactionQueue` vector associated with the provided queue ID.
    pub fn clear(&self, queue_id: u64) -> Result<()> {
        self.with_transaction_queue(queue_id, TransactionQueue::clear)
    }

    /// `drain_all` returns all transactions from the `TransactionQueue` associated with the
    /// provided queue ID, removing them from the queue. Transactions are returned in the
    /// order they were added.
    pub fn drain_all(&self, queue_id: u64) -> Result<Vec<QueueTxItem>> {
        self.with_transaction_queue(queue_id, TransactionQueue::drain_all)
    }

    /// Counts the number of transactions in the queue associated with the queue ID.
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    ///
    pub fn count(&self, queue_id: u64) -> Result<usize> {
        self.with_transaction_queue(queue_id, TransactionQueue::len)
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
    /// Returns `QueueError::InvalidFee` if the fee calculation overflows.
    ///
    pub fn queue_tx(
        &self,
        queue_id: u64,
        tx: QueueTx,
        hash: NativeTxHash,
        gas_used: U256,
        base_fee: U256,
    ) -> Result<()> {
        self.with_transaction_queue(queue_id, |queue| {
            queue.queue_tx(tx, hash, gas_used, base_fee)
        })
        .and_then(|res| res)
    }

    /// Removes all transactions in the queue whose sender matches the provided sender address.
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    ///
    pub fn remove_txs_by_sender(&self, queue_id: u64, sender: H160) -> Result<()> {
        self.with_transaction_queue(queue_id, |queue| queue.remove_txs_by_sender(sender))
    }

    pub fn get_queue(&self, queue_id: u64) -> Result<Arc<TransactionQueue>> {
        Ok(Arc::clone(
            self.queues
                .read()
                .unwrap()
                .get(&queue_id)
                .ok_or(QueueError::NoSuchQueue)?,
        ))
    }

    pub fn get_tx_queue_items(&self, queue_id: u64) -> Result<Vec<QueueTxItem>> {
        self.with_transaction_queue(queue_id, TransactionQueue::get_tx_queue_items)
    }

    /// `get_next_valid_nonce` returns the next valid nonce for the account with the provided address
    /// in the `TransactionQueue` associated with the provided queue ID. This method assumes that
    /// only signed transactions (which include a nonce) are added to the queue using `queue_tx`
    /// and that their nonces are in increasing order.
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    ///
    /// Returns None when the address does not match an account or Some(nonce) with the next valid nonce (current + 1)
    /// for the associated address
    pub fn get_next_valid_nonce(&self, queue_id: u64, address: H160) -> Result<Option<U256>> {
        self.with_transaction_queue(queue_id, |queue| queue.get_next_valid_nonce(address))
    }

    pub fn get_total_gas_used(&self, queue_id: u64) -> Result<U256> {
        self.with_transaction_queue(queue_id, |queue| queue.get_total_gas_used())
    }

    /// Apply the closure to the queue associated with the queue ID.
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchQueue` if no queue is associated with the given queue ID.
    pub fn with_transaction_queue<T, F>(&self, queue_id: u64, f: F) -> Result<T>
    where
        F: FnOnce(&TransactionQueue) -> T,
    {
        match self.queues.read().unwrap().get(&queue_id) {
            Some(queue) => Ok(f(queue)),
            None => Err(QueueError::NoSuchQueue),
        }
    }
}

#[derive(Debug, Clone)]
pub enum QueueTx {
    SignedTx(Box<SignedTx>),
    SystemTx(SystemTx),
}

#[derive(Debug, Clone)]
pub struct QueueTxItem {
    pub queue_tx: QueueTx,
    pub tx_hash: NativeTxHash,
    pub tx_fee: U256,
    pub gas_used: U256,
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
/// 4. Total gas fees of all queued transactions
/// 5. Total gas used by all queued transactions
///
/// It's used to manage and process transactions for different accounts.
///
#[derive(Clone, Debug, Default)]
pub struct TransactionQueueData {
    pub transactions: Vec<QueueTxItem>,
    pub account_nonces: HashMap<H160, U256>,
    pub block_data: Option<BlockData>,
    pub total_fees: U256,
    pub total_gas_used: U256,
}

impl TransactionQueueData {
    pub fn new() -> Self {
        Self {
            transactions: Vec::new(),
            account_nonces: HashMap::new(),
            total_fees: U256::zero(),
            total_gas_used: U256::zero(),
            block_data: None,
        }
    }
}

#[derive(Debug, Default)]
pub struct TransactionQueue {
    pub data: Mutex<TransactionQueueData>,
}

impl TransactionQueue {
    fn new() -> Self {
        Self {
            data: Mutex::new(TransactionQueueData::new()),
        }
    }

    pub fn clear(&self) {
        let mut data = self.data.lock().unwrap();
        data.account_nonces.clear();
        data.total_fees = U256::zero();
        data.total_gas_used = U256::zero();
        data.transactions.clear();
    }

    pub fn drain_all(&self) -> Vec<QueueTxItem> {
        let mut data = self.data.lock().unwrap();
        data.account_nonces.clear();
        data.total_fees = U256::zero();
        data.total_gas_used = U256::zero();
        data.transactions.drain(..).collect::<Vec<QueueTxItem>>()
    }

    pub fn len(&self) -> usize {
        self.data.lock().unwrap().transactions.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.lock().unwrap().transactions.is_empty()
    }

    pub fn queue_tx(
        &self,
        tx: QueueTx,
        tx_hash: NativeTxHash,
        gas_used: U256,
        base_fee: U256,
    ) -> Result<()> {
        let mut gas_fee = U256::zero();
        let mut data = self.data.lock().unwrap();
        if let QueueTx::SignedTx(signed_tx) = &tx {
            if let Some(nonce) = data.account_nonces.get(&signed_tx.sender) {
                if signed_tx.nonce() != nonce + 1 {
                    return Err(QueueError::InvalidNonce((signed_tx.clone(), *nonce)));
                }
            }
            data.account_nonces
                .insert(signed_tx.sender, signed_tx.nonce());

            gas_fee = match calculate_gas_fee(signed_tx, gas_used, base_fee) {
                Ok(fee) => fee,
                Err(_) => return Err(QueueError::InvalidFee),
            };
            data.total_fees += gas_fee;
            data.total_gas_used += gas_used;
        }
        data.transactions.push(QueueTxItem {
            queue_tx: tx,
            tx_hash,
            tx_fee: gas_fee,
            gas_used,
        });
        Ok(())
    }

    pub fn remove_txs_by_sender(&self, sender: H160) {
        let mut data = self.data.lock().unwrap();
        let mut fees_to_remove = U256::zero();
        let mut gas_used_to_remove = U256::zero();
        data.transactions.retain(|item| {
            let tx_sender = match &item.queue_tx {
                QueueTx::SignedTx(tx) => tx.sender,
                QueueTx::SystemTx(tx) => tx.sender().unwrap_or_default(),
            };
            if tx_sender == sender {
                fees_to_remove += item.tx_fee;
                gas_used_to_remove += item.gas_used;
                return false;
            }
            true
        });
        data.total_fees -= fees_to_remove;
        data.total_gas_used -= gas_used_to_remove;
        data.account_nonces.remove(&sender);
    }

    pub fn get_tx_queue_items(&self) -> Vec<QueueTxItem> {
        self.data.lock().unwrap().transactions.clone()
    }

    pub fn get_next_valid_nonce(&self, address: H160) -> Option<U256> {
        self.data
            .lock()
            .unwrap()
            .account_nonces
            .get(&address)
            .map(ToOwned::to_owned)
            .map(|nonce| nonce + 1)
    }

    pub fn get_total_gas_used(&self) -> U256 {
        self.data.lock().unwrap().total_gas_used
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
    InvalidFee,
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            QueueError::NoSuchQueue => write!(f, "No transaction queue for this queue"),
            QueueError::InvalidNonce((tx, nonce)) => write!(f, "Invalid nonce {:x?} for tx {:x?}. Previous queued nonce is {}. TXs should be queued in increasing nonce order.", tx.nonce(), tx.transaction.hash(), nonce),
            QueueError::InvalidFee => write!(f, "Invalid transaction fee from value overflow"),
        }
    }
}

impl std::error::Error for QueueError {}

#[cfg(test_off)]
mod tests {
    use std::str::FromStr;

    use ethereum_types::{H256, U256};

    use crate::transaction::bridge::BalanceUpdate;

    use super::*;

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
