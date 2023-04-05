use primitive_types::{H160, U256};
use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Mutex, RwLock},
};

use ain_evm::transaction::SignedTx;

use crate::EVMState;

#[derive(Debug)]
pub struct TransactionQueueMap {
    queues: RwLock<HashMap<u64, TransactionQueue>>,
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

            if !write_guard.contains_key(&context) {
                write_guard.insert(context, TransactionQueue::new(state));
                return context;
            }
        }
    }

    pub fn remove(&self, index: u64) -> Option<TransactionQueue> {
        self.queues.write().unwrap().remove(&index)
    }

    pub fn clear(&self, index: u64) {
        if let Some(queue) = self.queues.read().unwrap().get(&index) {
            queue.clear()
        }
    }

    pub fn add_signed_tx(&self, index: u64, signed_tx: SignedTx) {
        if let Some(queue) = self.queues.read().unwrap().get(&index) {
            queue.add_signed_tx(signed_tx)
        }
    }

    pub fn drain_all(&self, index: u64) -> Vec<SignedTx> {
        match self.queues.read().unwrap().get(&index) {
            Some(queue) => queue.drain_all(),
            None => Vec::new(),
        }
    }

    pub fn len(&self, index: u64) -> usize {
        match self.queues.read().unwrap().get(&index) {
            Some(queue) => queue.len(),
            None => 0,
        }
    }

    pub fn add_balance(&self, index: u64, address: H160, value: U256) {
        if let Some(queue) = self.queues.read().unwrap().get(&index) {
            queue.add_balance(address, value)
        }
    }

    pub fn sub_balance(
        &self,
        index: u64,
        address: H160,
        value: U256,
    ) -> Result<(), Box<dyn std::error::Error>> {
        if let Some(queue) = self.queues.read().unwrap().get(&index) {
            queue.sub_balance(address, value)
        } else {
            Err(Box::new(ErrorStr(
                "No transaction queue for this context".into(),
            )))
        }
    }

    pub fn state(&self, index: u64) -> Option<EVMState> {
        if let Some(queue) = self.queues.read().unwrap().get(&index) {
            Some(queue.state())
        } else {
            None
        }
    }
}

#[derive(Debug)]
pub struct TransactionQueue {
    txs: Mutex<Vec<SignedTx>>,
    state: RwLock<EVMState>,
}

impl TransactionQueue {
    pub fn new(state: EVMState) -> Self {
        Self {
            txs: Mutex::new(Vec::new()),
            state: RwLock::new(state),
        }
    }

    pub fn clear(&self) {
        self.txs.lock().unwrap().clear()
    }

    pub fn drain_all(&self) -> Vec<SignedTx> {
        self.txs
            .lock()
            .unwrap()
            .drain(..)
            .collect::<Vec<SignedTx>>()
    }

    pub fn add_signed_tx(&self, signed_tx: SignedTx) {
        self.txs.lock().unwrap().push(signed_tx)
    }

    pub fn len(&self) -> usize {
        self.txs.lock().unwrap().len()
    }

    pub fn state(&self) -> EVMState {
        self.state.read().unwrap().clone()
    }

    pub fn add_balance(&self, address: H160, value: U256) {
        let mut state = self.state.write().unwrap();
        let mut account = state.entry(address).or_default();
        account.balance = account.balance + value;
    }

    pub fn sub_balance(
        &self,
        address: H160,
        value: U256,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let mut state = self.state.write().unwrap();
        let mut account = state.get_mut(&address).unwrap();
        if account.balance > value {
            account.balance = account.balance - value;
            return Ok(());
        }
        Err(Box::new(ErrorStr("Sub balance failed".into())))
    }
}

#[derive(Debug)]
struct ErrorStr(String);

impl std::fmt::Display for ErrorStr {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "Error: {}", self.0)
    }
}

impl std::error::Error for ErrorStr {}
