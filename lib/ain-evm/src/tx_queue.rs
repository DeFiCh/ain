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

impl TransactionQueueMap {
    pub fn new() -> Self {
        TransactionQueueMap {
            queues: RwLock::new(HashMap::new()),
        }
    }

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

    pub fn remove(&self, context_id: u64) -> Option<TransactionQueue> {
        self.queues.write().unwrap().remove(&context_id)
    }

    pub fn clear(&self, context_id: u64) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .ok_or(QueueError::NoSuchContext)
            .map(TransactionQueue::clear)
    }

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
            .map(|queue| queue.queue_tx((tx, hash)))
    }

    pub fn drain_all(&self, context_id: u64) -> Vec<QueueTxWithNativeHash> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map_or(Vec::new(), TransactionQueue::drain_all)
    }

    pub fn get_cloned_vec(&self, context_id: u64) -> Vec<QueueTxWithNativeHash> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map_or(Vec::new(), TransactionQueue::get_cloned_vec)
    }

    pub fn len(&self, context_id: u64) -> usize {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map_or(0, TransactionQueue::len)
    }
}

#[derive(Debug, Clone)]
pub enum QueueTx {
    SignedTx(Box<SignedTx>),
    BridgeTx(BridgeTx),
}

type QueueTxWithNativeHash = (QueueTx, NativeTxHash);

#[derive(Debug, Default)]
pub struct TransactionQueue {
    transactions: Mutex<Vec<QueueTxWithNativeHash>>,
}

impl TransactionQueue {
    pub fn new() -> Self {
        Self {
            transactions: Mutex::new(Vec::new()),
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

    pub fn get_cloned_vec(&self) -> Vec<QueueTxWithNativeHash> {
        self.transactions.lock().unwrap().clone()
    }

    pub fn queue_tx(&self, tx: QueueTxWithNativeHash) {
        self.transactions.lock().unwrap().push(tx);
    }

    pub fn len(&self) -> usize {
        self.transactions.lock().unwrap().len()
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
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            QueueError::NoSuchContext => write!(f, "No transaction queue for this context"),
        }
    }
}

impl std::error::Error for QueueError {}
