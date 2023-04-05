use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Mutex, RwLock},
};

use ain_evm::transaction::SignedTx;

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

    pub fn get_context(&self) -> u64 {
        let mut rng = rand::thread_rng();
        loop {
            let context = rng.gen();
            let mut write_guard = self.queues.write().unwrap();

            if !write_guard.contains_key(&context) {
                write_guard.insert(context, TransactionQueue::new());
                return context;
            }
        }
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
}

#[derive(Debug)]
pub struct TransactionQueue {
    txs: Mutex<Vec<SignedTx>>,
}

impl TransactionQueue {
    pub fn new() -> Self {
        Self {
            txs: Mutex::new(Vec::new()),
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
}
