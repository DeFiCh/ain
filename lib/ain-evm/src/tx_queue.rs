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

    pub fn len(&self, context_id: u64) -> usize {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map_or(0, TransactionQueue::len)
    }

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

    pub fn queue_tx(&self, tx: QueueTxWithNativeHash) {
        if let QueueTx::SignedTx(signed_tx) = &tx.0 {
            self.account_nonces
                .lock()
                .unwrap()
                .insert(signed_tx.sender, signed_tx.nonce());
        }
        self.transactions.lock().unwrap().push(tx);
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
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            QueueError::NoSuchContext => write!(f, "No transaction queue for this context"),
        }
    }
}

impl std::error::Error for QueueError {}
