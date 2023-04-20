use primitive_types::{H160, U256};
use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Mutex, RwLock},
};

use crate::transaction::SignedTx;

use crate::evm::EVMState;

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

    pub fn get_context(&self, state: EVMState) -> u64 {
        let mut rng = rand::thread_rng();
        loop {
            let context = rng.gen();
            let mut write_guard = self.queues.write().unwrap();

            if let std::collections::hash_map::Entry::Vacant(e) = write_guard.entry(context) {
                e.insert(TransactionQueue::new(state));
                return context;
            }
        }
    }

    pub fn remove(&self, context_id: u64) -> Option<TransactionQueue> {
        self.queues.write().unwrap().remove(&context_id)
    }

    pub fn clear(&self, context_id: u64) {
        if let Some(queue) = self.queues.read().unwrap().get(&context_id) {
            queue.clear()
        }
    }

    pub fn add_signed_tx(&self, context_id: u64, signed_tx: SignedTx) {
        if let Some(queue) = self.queues.read().unwrap().get(&context_id) {
            queue.add_signed_tx(signed_tx)
        }
    }

    pub fn drain_all(&self, context_id: u64) -> Vec<SignedTx> {
        match self.queues.read().unwrap().get(&context_id) {
            Some(queue) => queue.drain_all(),
            None => Vec::new(),
        }
    }

    pub fn len(&self, context_id: u64) -> usize {
        match self.queues.read().unwrap().get(&context_id) {
            Some(queue) => queue.len(),
            None => 0,
        }
    }

    pub fn add_balance(&self, context_id: u64, address: H160, value: U256) {
        if let Some(queue) = self.queues.read().unwrap().get(&context_id) {
            queue.add_balance(address, value)
        }
    }

    pub fn sub_balance(
        &self,
        context_id: u64,
        address: H160,
        value: U256,
    ) -> Result<(), QueueError> {
        if let Some(queue) = self.queues.read().unwrap().get(&context_id) {
            queue.sub_balance(address, value)
        } else {
            Err(QueueError::NoSuchContext)
        }
    }

    pub fn state(&self, context_id: u64) -> Option<EVMState> {
        self.queues
            .read()
            .unwrap()
            .get(&context_id)
            .map(|queue| queue.state())
    }
}

#[derive(Debug)]
pub struct TransactionQueue {
    transactions: Mutex<Vec<SignedTx>>,
    state: RwLock<EVMState>,
}

impl TransactionQueue {
    pub fn new(state: EVMState) -> Self {
        Self {
            transactions: Mutex::new(Vec::new()),
            state: RwLock::new(state),
        }
    }

    pub fn clear(&self) {
        self.transactions.lock().unwrap().clear()
    }

    pub fn drain_all(&self) -> Vec<SignedTx> {
        self.transactions
            .lock()
            .unwrap()
            .drain(..)
            .collect::<Vec<SignedTx>>()
    }

    pub fn add_signed_tx(&self, signed_tx: SignedTx) {
        self.transactions.lock().unwrap().push(signed_tx)
    }

    pub fn len(&self) -> usize {
        self.transactions.lock().unwrap().len()
    }

    pub fn state(&self) -> EVMState {
        self.state.read().unwrap().clone()
    }

    pub fn add_balance(&self, address: H160, value: U256) {
        let mut state = self.state.write().unwrap();
        let account = state.entry(address).or_default();
        account.balance += value;
    }

    pub fn sub_balance(&self, address: H160, value: U256) -> Result<(), QueueError> {
        let mut state = self.state.write().unwrap();
        let account = state.get_mut(&address).unwrap();
        if account.balance >= value {
            account.balance -= value;
            Ok(())
        } else {
            Err(QueueError::InsufficientBalance)
        }
    }
}

#[derive(Debug)]
pub enum QueueError {
    NoSuchContext,
    InsufficientBalance,
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            QueueError::NoSuchContext => write!(f, "No transaction queue for this context"),
            QueueError::InsufficientBalance => write!(f, "Insufficient balance"),
        }
    }
}

impl std::error::Error for QueueError {}
