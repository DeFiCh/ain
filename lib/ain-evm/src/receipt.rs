use crate::traits::{PersistentState, PersistentStateError};
use crate::transaction::SignedTx;
use ethereum::{EIP658ReceiptData, EnvelopedEncodable, ReceiptV3};
use primitive_types::{H160, H256, U256};

use ethereum::util::ordered_trie_root;
use keccak_hash::keccak;
use rlp::RlpStream;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::error::Error;
use std::sync::{Arc, RwLock};

pub static RECEIPT_MAP_PATH: &str = "receipt_map.bin";

#[derive(Serialize, Deserialize, Clone)]
pub struct Receipt {
    pub tx_hash: H256,
    pub receipt: ReceiptV3,
    pub block_hash: H256,
    pub block_number: U256,
    pub from: H160,
    pub to: Option<H160>,
    pub tx_index: usize,
    pub tx_type: u8,
    pub contract_address: Option<H160>,
}

type TransactionHashToReceipt = HashMap<H256, Receipt>;

pub struct ReceiptHandler {
    pub transaction_map: Arc<RwLock<TransactionHashToReceipt>>,
}

impl PersistentState for TransactionHashToReceipt {}

impl Default for ReceiptHandler {
    fn default() -> Self {
        Self::new()
    }
}

fn get_contract_address(to: &Option<H160>, sender: &H160, nonce: &U256) -> Option<H160> {
    if to.is_some() {
        return None;
    }

    let mut stream = RlpStream::new_list(2);
    stream.append(sender);
    stream.append(nonce);

    return Some(H160::from(keccak(stream.as_raw())));
}

impl ReceiptHandler {
    pub fn new() -> Self {
        Self {
            transaction_map: Arc::new(RwLock::new(
                TransactionHashToReceipt::load_from_disk(RECEIPT_MAP_PATH).unwrap(),
            )),
        }
    }

    pub fn flush(&self) -> Result<(), PersistentStateError> {
        self.transaction_map
            .write()
            .unwrap()
            .save_to_disk(RECEIPT_MAP_PATH)
    }

    pub fn get_receipt(&self, tx: H256) -> Result<Receipt, ReceiptHandlerError> {
        let map = self.transaction_map.read().unwrap();

        let receipt = map
            .get(&tx)
            .ok_or(ReceiptHandlerError::ReceiptNotFound)?
            .clone();
        Ok(receipt)
    }

    pub fn generate_receipts(
        &self,
        successful: Vec<SignedTx>,
        failed: Vec<SignedTx>,
        block_hash: H256,
        block_number: U256,
    ) -> H256 {
        let mut map = self.transaction_map.write().unwrap();
        let mut receipts = Vec::new();

        let mut index = 0;

        for transaction in successful {
            let tv2 = transaction.clone().transaction;
            let receipt = Receipt {
                receipt: ReceiptV3::EIP1559(EIP658ReceiptData {
                    status_code: 1,
                    used_gas: Default::default(),
                    logs_bloom: Default::default(),
                    logs: vec![],
                }),
                block_hash,
                block_number,
                tx_hash: tv2.hash(),
                from: transaction.sender,
                to: transaction.to(),
                tx_index: index,
                tx_type: EnvelopedEncodable::type_id(&tv2).unwrap_or_default(),
                contract_address: get_contract_address(
                    &transaction.to(),
                    &transaction.sender,
                    &transaction.nonce(),
                ),
            };

            map.insert(tv2.hash(), receipt.clone());
            receipts.push(receipt);
            index += 1;
        }

        for transaction in failed {
            let tv2 = transaction.clone().transaction;
            let receipt = Receipt {
                receipt: ReceiptV3::EIP1559(EIP658ReceiptData {
                    status_code: 0,
                    used_gas: Default::default(),
                    logs_bloom: Default::default(),
                    logs: vec![],
                }),
                block_hash,
                block_number,
                tx_hash: tv2.hash(),
                from: transaction.sender,
                to: transaction.to(),
                tx_index: index,
                contract_address: get_contract_address(
                    &transaction.to(),
                    &transaction.sender,
                    &transaction.nonce(),
                ),
                tx_type: EnvelopedEncodable::type_id(&tv2).unwrap(),
            };

            map.insert(tv2.hash(), receipt.clone());
            receipts.push(receipt);
            index += 1;
        }

        let root = ordered_trie_root(
            receipts
                .iter()
                .map(|r| EnvelopedEncodable::encode(&r.receipt).freeze()),
        );

        return root;
    }
}

use std::fmt;

#[derive(Debug)]
pub enum ReceiptHandlerError {
    ReceiptNotFound,
}

impl fmt::Display for ReceiptHandlerError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ReceiptHandlerError::ReceiptNotFound => write!(f, "Receipt not found"),
        }
    }
}

impl Error for ReceiptHandlerError {}

#[cfg(test)]
mod test {
    use crate::receipt::get_contract_address;
    use keccak_hash::keccak;
    use primitive_types::{H160, U256};
    use rlp::RlpStream;
    use std::str::FromStr;

    #[test]
    pub fn test_contract_address() {
        let sender = H160::from_str("0f572e5295c57f15886f9b263e2f6d2d6c7b5ec6").unwrap();

        let expected = H160::from_str("3f09c73a5ed19289fb9bdc72f1742566df146f56").unwrap();
        let to = H160::from_str("3f09c73a5ed19289fb9bdc72f1742566df146f56").unwrap();

        let actual = get_contract_address(&Some(to), &sender, &U256::from(88));

        assert_eq!(actual.unwrap(), expected);
    }
}
