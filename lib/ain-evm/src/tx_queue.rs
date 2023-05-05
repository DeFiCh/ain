use primitive_types::{H160, U256};
use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Mutex, RwLock},
};

use crate::transaction::SignedTx;

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
            .map(|queue| queue.clear())
    }

    pub fn add_signed_tx(&self, context_id: u64, signed_tx: SignedTx) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .ok_or(QueueError::NoSuchContext)
            .map(|queue| queue.add_signed_tx(signed_tx))
    }

    pub fn drain_all(&self, context_id: u64) -> Vec<QueueTx> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map(|queue| queue.drain_all())
            .unwrap_or(Vec::new())
    }

    pub fn len(&self, context_id: u64) -> usize {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map(|queue| queue.len())
            .unwrap_or(0)
    }

    pub fn add_balance(
        &self,
        context_id: u64,
        address: H160,
        value: U256,
    ) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .ok_or(QueueError::NoSuchContext)
            .map(|queue| queue.add_balance(address, value))
    }

    pub fn sub_balance(
        &self,
        context_id: u64,
        address: H160,
        value: U256,
    ) -> Result<(), QueueError> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .ok_or(QueueError::NoSuchContext)
            .map(|queue| queue.sub_balance(address, value))
    }
}

#[derive(Debug)]
pub struct BalanceUpdate {
    pub address: H160,
    pub amount: U256,
}

#[derive(Debug)]
pub enum QueueTx {
    SignedTx(SignedTx),
    AddBalance(BalanceUpdate),
    SubBalance(BalanceUpdate),
}

#[derive(Debug)]
pub struct TransactionQueue {
    transactions: Mutex<Vec<QueueTx>>,
}

impl TransactionQueue {
    pub fn new() -> Self {
        Self {
            transactions: Mutex::new(Vec::new()),
        }
    }

    pub fn clear(&self) {
        self.transactions.lock().unwrap().clear()
    }

    pub fn drain_all(&self) -> Vec<QueueTx> {
        self.transactions
            .lock()
            .unwrap()
            .drain(..)
            .collect::<Vec<QueueTx>>()
    }

    pub fn add_signed_tx(&self, signed_tx: SignedTx) {
        self.transactions
            .lock()
            .unwrap()
            .push(QueueTx::SignedTx(signed_tx))
    }

    pub fn len(&self) -> usize {
        self.transactions.lock().unwrap().len()
    }

    pub fn add_balance(&self, address: H160, amount: U256) {
        self.transactions
            .lock()
            .unwrap()
            .push(QueueTx::AddBalance(BalanceUpdate { address, amount }))
    }

    pub fn sub_balance(&self, address: H160, amount: U256) {
        self.transactions
            .lock()
            .unwrap()
            .push(QueueTx::SubBalance(BalanceUpdate { address, amount }))
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
