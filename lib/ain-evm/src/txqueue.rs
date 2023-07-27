use ethereum_types::{H160, U256};
use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Mutex, RwLock},
};

use crate::{
    core::NativeTxHash,
    fee::calculate_gas_fee,
    transaction::{bridge::BridgeTx, SignedTx},
};
use crate::transaction::system::SystemTx;

#[derive(Debug)]
pub struct TransactionQueueMap {
    queues: RwLock<HashMap<u64, TransactionQueue>>,
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
                e.insert(TransactionQueue::new());
                return queue_id;
            }
        }
    }

    /// Try to remove and return the `TransactionQueue` associated with the provided
    /// queue ID.
    pub fn remove(&self, queue_id: u64) -> Option<TransactionQueue> {
        self.queues.write().unwrap().remove(&queue_id)
    }

    /// Clears the `TransactionQueue` vector associated with the provided queue ID.
    pub fn clear(&self, queue_id: u64) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .ok_or(QueueError::NoSuchContext)
            .map(TransactionQueue::clear)
    }

    /// Attempts to add a new transaction to the `TransactionQueue` associated with the
    /// provided queue ID. If the transaction is a `SignedTx`, it also updates the
    /// corresponding account's nonce.
    /// Nonces for each account's transactions must be in strictly increasing order. This means that if the last
    /// queued transaction for an account has nonce 3, the next one should have nonce 4. If a `SignedTx` with a nonce
    /// that is not one more than the previous nonce is added, an error is returned. This helps to ensure the integrity
    /// of the transaction queue and enforce correct nonce usage.
    ///
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchContext` if no queue is associated with the given queue ID.
    /// Returns `QueueError::InvalidNonce` if a `SignedTx` is provided with a nonce that is not one more than the
    /// previous nonce of transactions from the same sender in the queue.
    ///
    pub fn queue_tx(
        &self,
        queue_id: u64,
        tx: QueueTx,
        hash: NativeTxHash,
        gas_used: u64,
        base_fee: U256,
    ) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .ok_or(QueueError::NoSuchContext)
            .map(|queue| queue.queue_tx(tx, hash, gas_used, base_fee))?
    }

    /// `drain_all` returns all transactions from the `TransactionQueue` associated with the
    /// provided queue ID, removing them from the queue. Transactions are returned in the
    /// order they were added.
    pub fn drain_all(&self, queue_id: u64) -> Vec<QueueTxItem> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .map_or(Vec::new(), TransactionQueue::drain_all)
    }

    pub fn get_cloned_vec(&self, queue_id: u64) -> Vec<QueueTxItem> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .map_or(Vec::new(), TransactionQueue::get_cloned_vec)
    }

    pub fn len(&self, queue_id: u64) -> usize {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .map_or(0, TransactionQueue::len)
    }

    /// Removes all transactions in the queue whose sender matches the provided sender address.
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchContext` if no queue is associated with the given queue ID.
    ///
    pub fn remove_txs_by_sender(&self, queue_id: u64, sender: H160) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .ok_or(QueueError::NoSuchContext)
            .map(|queue| queue.remove_txs_by_sender(sender))
    }

    /// `get_next_valid_nonce` returns the next valid nonce for the account with the provided address
    /// in the `TransactionQueue` associated with the provided queue ID. This method assumes that
    /// only signed transactions (which include a nonce) are added to the queue using `queue_tx`
    /// and that their nonces are in increasing order.
    pub fn get_next_valid_nonce(&self, queue_id: u64, address: H160) -> Option<U256> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .and_then(|queue| queue.get_next_valid_nonce(address))
    }

    pub fn get_total_gas_used(&self, queue_id: u64) -> Option<u64> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .map(|queue| queue.get_total_gas_used())
    }

    pub fn get_total_fees(&self, queue_id: u64) -> Option<u64> {
        self.queues
            .read()
            .unwrap()
            .get(&queue_id)
            .map(|queue| queue.get_total_fees())
    }
}

#[derive(Debug, Clone)]
pub enum QueueTx {
    SignedTx(Box<SignedTx>),
    BridgeTx(BridgeTx),
    SystemTx(SystemTx),
}

#[derive(Debug, Clone)]
pub struct QueueTxItem {
    pub queue_tx: QueueTx,
    pub tx_hash: NativeTxHash,
    pub tx_fee: u64,
    pub gas_used: u64,
}

/// The `TransactionQueueData` holds a queue of transactions with a map of the account nonces,
/// the total gas fees and the total gas used by the transactions.
/// It's used to manage and process transactions for different accounts.
///
#[derive(Debug, Default)]
struct TransactionQueueData {
    transactions: Vec<QueueTxItem>,
    account_nonces: HashMap<H160, U256>,
    total_fees: u64,
    total_gas_used: u64,
}

impl TransactionQueueData {
    pub fn new() -> Self {
        Self {
            transactions: Vec::new(),
            account_nonces: HashMap::new(),
            total_fees: 0u64,
            total_gas_used: 0u64,
        }
    }
}

#[derive(Debug, Default)]
pub struct TransactionQueue {
    data: Mutex<TransactionQueueData>,
}

impl TransactionQueue {
    fn new() -> Self {
        Self {
            data: Mutex::new(TransactionQueueData::new()),
        }
    }

    pub fn clear(&self) {
        let mut data = self.data.lock().unwrap();
        data.total_fees = 0u64;
        data.total_gas_used = 0u64;
        data.transactions.clear();
    }

    pub fn drain_all(&self) -> Vec<QueueTxItem> {
        let mut data = self.data.lock().unwrap();
        data.total_fees = 0u64;
        data.total_gas_used = 0u64;
        data.transactions.drain(..).collect::<Vec<QueueTxItem>>()
    }

    pub fn get_cloned_vec(&self) -> Vec<QueueTxItem> {
        self.data.lock().unwrap().transactions.clone()
    }

    pub fn queue_tx(
        &self,
        tx: QueueTx,
        tx_hash: NativeTxHash,
        gas_used: u64,
        base_fee: U256,
    ) -> Result<(), QueueError> {
        let mut gas_fee: u64 = 0;
        let mut data = self.data.lock().unwrap();
        if let QueueTx::SignedTx(signed_tx) = &tx {
            if let Some(nonce) = data.account_nonces.get(&signed_tx.sender) {
                if signed_tx.nonce() != nonce + 1 {
                    return Err(QueueError::InvalidNonce((signed_tx.clone(), *nonce)));
                }
            }
            data.account_nonces
                .insert(signed_tx.sender, signed_tx.nonce());

            gas_fee = match calculate_gas_fee(signed_tx, gas_used.into(), base_fee) {
                Ok(fee) => fee.as_u64(),
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

    pub fn len(&self) -> usize {
        self.data.lock().unwrap().transactions.len()
    }

    pub fn remove_txs_by_sender(&self, sender: H160) {
        let mut data = self.data.lock().unwrap();
        let mut fees_to_remove = 0;
        let mut gas_used_to_remove = 0;
        data.transactions.retain(|item| {
            let tx_sender = match &item.queue_tx {
                QueueTx::SignedTx(tx) => tx.sender,
                QueueTx::BridgeTx(tx) => tx.sender(),
                QueueTx::SystemTx(_) => H160::zero(), // this is safe to do since we do not remove this transaction during mining
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

    pub fn get_next_valid_nonce(&self, address: H160) -> Option<U256> {
        self.data
            .lock()
            .unwrap()
            .account_nonces
            .get(&address)
            .map(ToOwned::to_owned)
            .map(|nonce| nonce + 1)
    }

    pub fn get_total_fees(&self) -> u64 {
        self.data.lock().unwrap().total_fees
    }

    pub fn get_total_gas_used(&self) -> u64 {
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
    NoSuchContext,
    InvalidNonce((Box<SignedTx>, U256)),
    InvalidFee,
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            QueueError::NoSuchContext => write!(f, "No transaction queue for this queue"),
            QueueError::InvalidNonce((tx, nonce)) => write!(f, "Invalid nonce {:x?} for tx {:x?}. Previous queued nonce is {}. TXs should be queued in increasing nonce order.", tx.nonce(), tx.transaction.hash(), nonce),
            QueueError::InvalidFee => write!(f, "Invalid transaction fee from value overflow"),
        }
    }
}

impl std::error::Error for QueueError {}

#[cfg(test)]
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

        queue.queue_tx(tx1, H256::from_low_u64_be(1).into(), 0u64, U256::zero())?;
        queue.queue_tx(tx2, H256::from_low_u64_be(2).into(), 0u64, U256::zero())?;
        // Should fail as nonce 2 is already queued for this sender
        let queued = queue.queue_tx(tx3, H256::from_low_u64_be(3).into(), 0u64, U256::zero());
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

        queue.queue_tx(tx1, H256::from_low_u64_be(1).into(), 0u64, U256::zero())?;
        queue.queue_tx(tx2, H256::from_low_u64_be(2).into(), 0u64, U256::zero())?;
        queue.queue_tx(tx3, H256::from_low_u64_be(3).into(), 0u64, U256::zero())?;
        // Should fail as nonce 2 is already queued for this sender
        let queued = queue.queue_tx(tx4, H256::from_low_u64_be(4).into(), 0u64, U256::zero());
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

        queue.queue_tx(tx1, H256::from_low_u64_be(1).into(), 0u64, U256::zero())?;
        queue.queue_tx(tx2, H256::from_low_u64_be(2).into(), 0u64, U256::zero())?;
        queue.queue_tx(tx3, H256::from_low_u64_be(3).into(), 0u64, U256::zero())?;
        queue.queue_tx(tx4, H256::from_low_u64_be(4).into(), 0u64, U256::zero())?;
        Ok(())
    }
}
