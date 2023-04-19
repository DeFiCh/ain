use crate::traits::{PersistentState, PersistentStateError};
use crate::transaction::SignedTx;
use ethereum::{
    BlockV2, EIP658ReceiptData, EnvelopedEncodable, ReceiptV3, TransactionAction, TransactionV2,
};
use primitive_types::{H128, H160, H256, U256};
use std::any::Any;
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;
use std::sync::{Arc, RwLock};

pub static RECEIPT_MAP_PATH: &str = "receipt_map.bin";

struct Receipt {
    receipt: ReceiptV3,
    block_hash: H256,
    block_number: U256,
    from: H160,
    to: H160,
    tx_index: usize,
    tx_type: u8,
}

type TransactionHashToReceipt = HashMap<H256, Receipt>;

pub struct ReceiptHandler {
    pub transaction_map: Arc<RwLock<TransactionHashToReceipt>>,
}

impl PersistentState for TransactionHashToReceipt {
    fn save_to_disk(&self, path: &str) -> Result<(), PersistentStateError> {
        let serialized_state = bincode::serialize(self)?;
        let mut file = File::create(path)?;
        file.write_all(&serialized_state)?;
        Ok(())
    }

    fn load_from_disk(path: &str) -> Result<Self, PersistentStateError> {
        if Path::new(path).exists() {
            let mut file = File::open(path)?;
            let mut data = Vec::new();
            file.read_to_end(&mut data)?;
            let new_state: HashMap<H256, Receipt> = bincode::deserialize(&data)?;
            Ok(new_state)
        } else {
            Ok(Self::new())
        }
    }
}

impl Default for ReceiptHandler {
    fn default() -> Self {
        Self::new()
    }
}

fn action(tx: TransactionV2) -> TransactionAction {
    match tx {
        TransactionV2::Legacy(t) => t.action,
        TransactionV2::EIP2930(t) => t.action,
        TransactionV2::EIP1559(t) => t.action,
    }
}

fn extr(ac: TransactionAction) -> H160 {
    match ac {
        TransactionAction::Create => H160::zero(),
        TransactionAction::Call(t) => t,
    }
}

impl ReceiptHandler {
    pub fn new() -> Self {
        Self {
            transaction_map: Arc::new(RwLock::new(
                BlockHashtoBlock::load_from_disk(RECEIPT_MAP_PATH).unwrap(),
            )),
        }
    }

    pub fn generate_receipts(
        &self,
        successful: Vec<SignedTx>,
        failed: Vec<SignedTx>,
        block_hash: H256,
        block_number: U256,
    ) {
        let mut map = self.transaction_map.write().unwrap();

        let mut index = 0;

        for transaction in successful {
            let tv2 = transaction.transaction;
            let receipt = Receipt {
                receipt: ReceiptV3::EIP1559(EIP658ReceiptData {
                    status_code: 1,
                    used_gas: Default::default(),
                    logs_bloom: Default::default(),
                    logs: vec![],
                }),
                block_hash,
                block_number,
                from: transaction.sender,
                to: extr(action(tv2)),
                tx_index: index,
                tx_type: transaction.transaction.type_id().unwrap(),
            };

            map.insert(tv2.hash(), receipt);
            index += 1;
        }

        for transaction in failed {
            let tv2 = transaction.transaction;
            let receipt = Receipt {
                receipt: ReceiptV3::EIP1559(EIP658ReceiptData {
                    status_code: 0,
                    used_gas: Default::default(),
                    logs_bloom: Default::default(),
                    logs: vec![],
                }),
                block_hash,
                block_number,
                from: transaction.sender,
                to: extr(action(tv2)),
                tx_index: index,
                tx_type: transaction.transaction.type_id().unwrap(),
            };

            map.insert(tv2.hash(), receipt);
            index += 1;
        }
    }
}
