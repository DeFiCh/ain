use ethereum_types::{H160, U256};
use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Mutex, RwLock},
};

use crate::{
    evm::NativeTxHash,
    transaction::{bridge::BridgeTx, SignedTx},
};

#[derive(Debug)]
pub struct TransactionQueueMap {
    queues: RwLock<HashMap<u64, TransactionQueue>>,
}

impl Default for TransactionQueueMap {
    fn default() -> Self {
        Self::new()
    }
}

/// Holds multiple `TransactionQueue`s, each associated with a unique context ID.
///
/// Context IDs are randomly generated and used to access distinct transaction queues.
impl TransactionQueueMap {
    pub fn new() -> Self {
        TransactionQueueMap {
            queues: RwLock::new(HashMap::new()),
        }
    }

    /// `get_context` generates a unique random ID, creates a new `TransactionQueue` for that ID,
    /// and then returns the ID.
    pub fn get_context(&self) -> u64 {
        let mut rng = rand::thread_rng();
        loop {
            let context = rng.gen();
            let mut write_guard = self.queues.write().unwrap();

            if let std::collections::hash_map::Entry::Vacant(e) = write_guard.entry(context) {
                e.insert(TransactionQueue::new());
                return context;
            }
        }
    }

    /// Try to remove and return the `TransactionQueue` associated with the provided
    /// context ID.
    pub fn remove(&self, context_id: u64) -> Option<TransactionQueue> {
        self.queues.write().unwrap().remove(&context_id)
    }

    /// Clears the `TransactionQueue` vector associated with the provided context ID.
    pub fn clear(&self, context_id: u64) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .ok_or(QueueError::NoSuchContext)
            .map(TransactionQueue::clear)
    }

    /// Attempts to add a new transaction to the `TransactionQueue` associated with the
    /// provided context ID. If the transaction is a `SignedTx`, it also updates the
    /// corresponding account's nonce.
    /// Nonces for each account's transactions must be in strictly increasing order. This means that if the last
    /// queued transaction for an account has nonce 3, the next one should have nonce 4. If a `SignedTx` with a nonce
    /// that is not one more than the previous nonce is added, an error is returned. This helps to ensure the integrity
    /// of the transaction queue and enforce correct nonce usage.
    ///
    /// # Errors
    ///
    /// Returns `QueueError::NoSuchContext` if no queue is associated with the given context ID.
    /// Returns `QueueError::InvalidNonce` if a `SignedTx` is provided with a nonce that is not one more than the
    /// previous nonce of transactions from the same sender in the queue.
    ///
    pub fn queue_tx(
        &self,
        context_id: u64,
        tx: QueueTx,
        hash: NativeTxHash,
    ) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .ok_or(QueueError::NoSuchContext)
            .map(|queue| queue.queue_tx((tx, hash)))?
    }

    /// `drain_all` returns all transactions from the `TransactionQueue` associated with the
    /// provided context ID, removing them from the queue. Transactions are returned in the
    /// order they were added.
    pub fn drain_all(&self, context_id: u64) -> Vec<QueueTxWithNativeHash> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map_or(Vec::new(), TransactionQueue::drain_all)
    }

    /// `len` returns the number of transactions in the `TransactionQueue` associated with the
    /// provided context ID.
    pub fn len(&self, context_id: u64) -> usize {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map_or(0, TransactionQueue::len)
    }

    /// `get_nonce` returns the latest nonce for the account with the provided address in the
    /// `TransactionQueue` associated with the provided context ID. This method assumes that
    /// only signed transactions (which include a nonce) are added to the queue using `queue_tx`
    /// and that their nonces are in increasing order.
    pub fn get_nonce(&self, context_id: u64, address: H160) -> Option<U256> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map_or(None, |queue| queue.get_nonce(address))
    }
}

#[derive(Debug)]
pub enum QueueTx {
    SignedTx(Box<SignedTx>),
    BridgeTx(BridgeTx),
}

type QueueTxWithNativeHash = (QueueTx, NativeTxHash);

/// The `TransactionQueue` holds a queue of transactions and a map of account nonces.
/// It's used to manage and process transactions for different accounts.
///
#[derive(Debug, Default)]
pub struct TransactionQueue {
    transactions: Mutex<Vec<QueueTxWithNativeHash>>,
    account_nonces: Mutex<HashMap<H160, U256>>,
}

impl TransactionQueue {
    pub fn new() -> Self {
        Self {
            transactions: Mutex::new(Vec::new()),
            account_nonces: Mutex::new(HashMap::new()),
        }
    }

    pub fn clear(&self) {
        self.transactions.lock().unwrap().clear();
    }

    pub fn drain_all(&self) -> Vec<QueueTxWithNativeHash> {
        self.transactions
            .lock()
            .unwrap()
            .drain(..)
            .collect::<Vec<QueueTxWithNativeHash>>()
    }

    pub fn queue_tx(&self, tx: QueueTxWithNativeHash) -> Result<(), QueueError> {
        if let QueueTx::SignedTx(signed_tx) = &tx.0 {
            let mut account_nonces = self.account_nonces.lock().unwrap();
            if let Some(nonce) = account_nonces.get(&signed_tx.sender) {
                if signed_tx.nonce() != nonce + 1 {
                    return Err(QueueError::InvalidNonce((signed_tx.clone(), *nonce)));
                }
            }
            account_nonces.insert(signed_tx.sender, signed_tx.nonce());
        }
        self.transactions.lock().unwrap().push(tx);
        Ok(())
    }

    pub fn len(&self) -> usize {
        self.transactions.lock().unwrap().len()
    }

    pub fn get_nonce(&self, address: H160) -> Option<U256> {
        self.account_nonces
            .lock()
            .unwrap()
            .get(&address)
            .map(ToOwned::to_owned)
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
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            QueueError::NoSuchContext => write!(f, "No transaction queue for this context"),
            QueueError::InvalidNonce((tx, nonce)) => write!(f, "Invalid nonce {:x?} for tx {:x?}. Previous queued nonce is {}. TXs should be queued in increasing nonce order.", tx.nonce(), tx.transaction.hash(), nonce),
        }
    }
}

impl std::error::Error for QueueError {}
