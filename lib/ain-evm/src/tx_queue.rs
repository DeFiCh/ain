use ethereum_types::H160;
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

    /// Drains all transactions from the queue and sorts SignedTx by nonce for each sender while keeping
    /// the order of BridgeTx stable.
    ///
    /// Transactions from a single sender must be processed in the order of their nonces,
    /// starting from 0. However, transactions from different senders can be processed in any order,
    /// and BridgeTx transactions should maintain their original order. This function is designed
    /// to handle these requirements.
    ///
    /// First, the function drains all transactions from the queue into a vector, then it partitions
    /// the SignedTx transactions by sender into a HashMap. This ensures that SignedTx transactions
    /// from the same sender are grouped together while leaving BridgeTx transactions unaffected.
    ///
    /// Then, for each group of SignedTx transactions from the same sender, it sorts them by nonce.
    /// This ensures that SignedTx transactions from a single sender are ordered correctly. BridgeTx
    /// transactions are not sorted and maintain their original order.
    ///
    /// Finally, it iterates over the original (unsorted) transactions. For each SignedTx transaction,
    /// it replaces it with the next (lowest nonce) SignedTx transaction from the same sender in the
    /// sorted transactions. This preserves the original order of senders while ensuring that SignedTx
    /// transactions from each sender are ordered by nonce. For BridgeTx transactions, they are left
    /// in their original order. The sorted transactions are then returned.
    ///
    /// Note that this sorting does not change the relative order of transactions from different senders,
    /// and it also does not change the order of BridgeTx transactions. It only sorts SignedTx transactions
    /// from the same sender by their nonces. This is important because it allows the transactions to be
    /// processed in a way that respects the nonce rules of Ethereum while preventing user abusing simple
    /// sort by nonce mechanism.
    pub fn drain_all(&self) -> Vec<QueueTxWithNativeHash> {
        let transactions = self
            .transactions
            .lock()
            .unwrap()
            .drain(..)
            .collect::<Vec<QueueTxWithNativeHash>>();

        // Partition signed transactions by sender
        let mut sender_signed_txs: HashMap<H160, Vec<SignedTx>> = HashMap::new();
        for tx in transactions.iter().filter_map(|(queue_tx, _)| {
            if let QueueTx::SignedTx(tx) = queue_tx {
                Some(tx)
            } else {
                None
            }
        }) {
            sender_signed_txs
                .entry(tx.sender.clone())
                .or_insert_with(Vec::new)
                .push(*tx.clone());
        }

        // Sort each sender's signed transactions
        for txs in sender_signed_txs.values_mut() {
            txs.sort_unstable_by(|tx1, tx2| tx1.nonce().cmp(&tx2.nonce()));
        }

        // Iterate over original transactions, replacing with sorted
        // Keeps bridge tx in place
        let mut sorted_transactions = Vec::new();
        for (tx, native_hash) in transactions {
            let sorted_tx = match tx {
                QueueTx::SignedTx(signed_tx) => QueueTx::SignedTx(Box::new(
                    sender_signed_txs
                        .get_mut(&signed_tx.sender)
                        .expect("Error rebuilding sorted vector")
                        .remove(0),
                )),
                QueueTx::BridgeTx(_) => tx,
            };
            sorted_transactions.push((sorted_tx, native_hash));
        }

        sorted_transactions
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

#[cfg(test)]
mod tests {
    use std::str::FromStr;

    use ethereum_types::{H256, U256};

    use super::*;

    impl QueueTx {
        fn nonce(&self) -> Option<U256> {
            match self {
                Self::SignedTx(tx) => Some(tx.nonce()),
                Self::BridgeTx(_) => None,
            }
        }

        fn sender(&self) -> H160 {
            match self {
                Self::BridgeTx(tx) => tx.address,
                Self::SignedTx(tx) => tx.sender,
            }
        }
    }

    #[test]
    fn test_drain_all_sorting_keep_sender_order() {
        let queue = TransactionQueue::new();

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx2 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx3 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        // Nonce 1, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx4 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869018502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0dd1fad9a8465969354d567e8a74af3f6de3e56abbe1b71154d7929d0bd5cc170a0353190adb50e3cfae82a77c2ea56b49a86f72bd0071a70d1c25c49827654aa68").unwrap()));

        // Nonce 1, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx5 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869018502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0aeab138b2149ef0016d6bdd9819ed40a2411d5a577f05bcb50088e0a0dbadd06a031ac3e730b1b4751c3189284bad40bfe40ace03069f35adb2cb191c5ed2bc27f").unwrap()));

        // Nonce 0, sender 0xb30cbd96260323e3558132404df721e7ea1b2034
        let tx6 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0ca4b1d4ff6c090cf281131c0c91beeb300326aeb0049f854b1e622d59562ae59a00a85fdee6607ece9b2cc41d62da89fc488ce8c765437e60b4480fe8244a0ff61").unwrap()));

        queue.queue_tx((tx1, H256::from_low_u64_be(1).into()));
        queue.queue_tx((tx2, H256::from_low_u64_be(2).into()));
        queue.queue_tx((tx3, H256::from_low_u64_be(3).into()));
        queue.queue_tx((tx4, H256::from_low_u64_be(4).into()));
        queue.queue_tx((tx5, H256::from_low_u64_be(5).into()));
        queue.queue_tx((tx6, H256::from_low_u64_be(6).into()));

        {
            let transactions = queue.transactions.lock().unwrap();

            // Assert that sender and nonce are in the expected initial order
            assert_eq!(
                transactions[0].0.sender(),
                H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
            );
            assert_eq!(transactions[0].0.nonce().unwrap(), U256::from(2));
            assert_eq!(
                transactions[1].0.sender(),
                H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
            );
            assert_eq!(transactions[1].0.nonce().unwrap(), U256::from(2));
            assert_eq!(
                transactions[2].0.sender(),
                H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
            );
            assert_eq!(transactions[2].0.nonce().unwrap(), U256::from(0));
            assert_eq!(
                transactions[3].0.sender(),
                H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
            );
            assert_eq!(transactions[3].0.nonce().unwrap(), U256::from(1));
            assert_eq!(
                transactions[4].0.sender(),
                H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
            );
            assert_eq!(transactions[4].0.nonce().unwrap(), U256::from(1));
            assert_eq!(
                transactions[5].0.sender(),
                H160::from_str("b30cbd96260323e3558132404df721e7ea1b2034").unwrap()
            );
            assert_eq!(transactions[5].0.nonce().unwrap(), U256::from(0));
        }

        let drained = queue.drain_all();

        // Assert that sender and nonce are in the correct sorted order
        assert_eq!(
            drained[0].0.sender(),
            H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
        );
        assert_eq!(drained[0].0.nonce().unwrap(), U256::from(0));
        assert_eq!(
            drained[1].0.sender(),
            H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
        );
        assert_eq!(drained[1].0.nonce().unwrap(), U256::from(1));
        assert_eq!(
            drained[2].0.sender(),
            H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
        );
        assert_eq!(drained[2].0.nonce().unwrap(), U256::from(1));
        assert_eq!(
            drained[3].0.sender(),
            H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
        );
        assert_eq!(drained[3].0.nonce().unwrap(), U256::from(2));
        assert_eq!(
            drained[4].0.sender(),
            H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
        );
        assert_eq!(drained[4].0.nonce().unwrap(), U256::from(2));
        assert_eq!(
            drained[5].0.sender(),
            H160::from_str("b30cbd96260323e3558132404df721e7ea1b2034").unwrap()
        );
        assert_eq!(drained[5].0.nonce().unwrap(), U256::from(0));
    }

    #[test]
    fn test_drain_all_sorting_bridge_tx() {
        let queue = TransactionQueue::new();

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx2 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx3 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        // Nonce 1, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx4 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869018502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0dd1fad9a8465969354d567e8a74af3f6de3e56abbe1b71154d7929d0bd5cc170a0353190adb50e3cfae82a77c2ea56b49a86f72bd0071a70d1c25c49827654aa68").unwrap()));

        // Nonce 1, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx5 = QueueTx::BridgeTx(BridgeTx {
            r#type: crate::transaction::bridge::BridgeTxType::EvmIn,
            address: H160::zero(),
            amount: U256::one(),
        });

        // Nonce 1, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx6 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869018502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0aeab138b2149ef0016d6bdd9819ed40a2411d5a577f05bcb50088e0a0dbadd06a031ac3e730b1b4751c3189284bad40bfe40ace03069f35adb2cb191c5ed2bc27f").unwrap()));

        // Nonce 1, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx7 = QueueTx::BridgeTx(BridgeTx {
            r#type: crate::transaction::bridge::BridgeTxType::EvmIn,
            address: H160::zero(),
            amount: U256::one(),
        });

        // Nonce 0, sender 0xb30cbd96260323e3558132404df721e7ea1b2034
        let tx8 = QueueTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0ca4b1d4ff6c090cf281131c0c91beeb300326aeb0049f854b1e622d59562ae59a00a85fdee6607ece9b2cc41d62da89fc488ce8c765437e60b4480fe8244a0ff61").unwrap()));

        queue.queue_tx((tx1, H256::from_low_u64_be(1).into()));
        queue.queue_tx((tx2, H256::from_low_u64_be(2).into()));
        queue.queue_tx((tx3, H256::from_low_u64_be(3).into()));
        queue.queue_tx((tx4, H256::from_low_u64_be(4).into()));
        queue.queue_tx((tx5, H256::from_low_u64_be(5).into()));
        queue.queue_tx((tx6, H256::from_low_u64_be(6).into()));
        queue.queue_tx((tx7, H256::from_low_u64_be(7).into()));
        queue.queue_tx((tx8, H256::from_low_u64_be(8).into()));

        {
            let transactions = queue.transactions.lock().unwrap();

            // Assert that sender and nonce are in the expected initial order
            assert_eq!(
                transactions[0].0.sender(),
                H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
            );
            assert_eq!(transactions[0].0.nonce().unwrap(), U256::from(2));
            assert_eq!(
                transactions[1].0.sender(),
                H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
            );
            assert_eq!(transactions[1].0.nonce().unwrap(), U256::from(2));
            assert_eq!(
                transactions[2].0.sender(),
                H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
            );
            assert_eq!(transactions[2].0.nonce().unwrap(), U256::from(0));
            assert_eq!(
                transactions[3].0.sender(),
                H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
            );
            assert_eq!(transactions[3].0.nonce().unwrap(), U256::from(1));
            assert_eq!(transactions[4].0.sender(), H160::zero());
            assert_eq!(transactions[4].0.nonce(), None);
            assert_eq!(
                transactions[5].0.sender(),
                H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
            );
            assert_eq!(transactions[5].0.nonce().unwrap(), U256::from(1));
            assert_eq!(transactions[6].0.sender(), H160::zero());
            assert_eq!(transactions[6].0.nonce(), None);
            assert_eq!(
                transactions[7].0.sender(),
                H160::from_str("b30cbd96260323e3558132404df721e7ea1b2034").unwrap()
            );
            assert_eq!(transactions[7].0.nonce().unwrap(), U256::from(0));
        }

        let drained = queue.drain_all();

        // Assert that sender and nonce are in the correct sorted order
        assert_eq!(
            drained[0].0.sender(),
            H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
        );
        assert_eq!(drained[0].0.nonce().unwrap(), U256::from(0));
        assert_eq!(
            drained[1].0.sender(),
            H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
        );
        assert_eq!(drained[1].0.nonce().unwrap(), U256::from(1));
        assert_eq!(
            drained[2].0.sender(),
            H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
        );
        assert_eq!(drained[2].0.nonce().unwrap(), U256::from(1));
        assert_eq!(
            drained[3].0.sender(),
            H160::from_str("e61a3a6eb316d773c773f4ce757a542f673023c6").unwrap()
        );
        assert_eq!(drained[3].0.nonce().unwrap(), U256::from(2));
        assert_eq!(drained[4].0.sender(), H160::zero());
        assert_eq!(drained[4].0.nonce(), None);
        assert_eq!(
            drained[5].0.sender(),
            H160::from_str("6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap()
        );
        assert_eq!(drained[5].0.nonce().unwrap(), U256::from(2));
        assert_eq!(drained[6].0.sender(), H160::zero());
        assert_eq!(drained[6].0.nonce(), None);
        assert_eq!(
            drained[7].0.sender(),
            H160::from_str("b30cbd96260323e3558132404df721e7ea1b2034").unwrap()
        );
        assert_eq!(drained[7].0.nonce().unwrap(), U256::from(0));
    }
}
