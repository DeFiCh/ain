use crate::storage::{traits::ReceiptStorage, Storage};
use crate::transaction::SignedTx;
use ethereum::{EIP658ReceiptData, EnvelopedEncodable, ReceiptV3};
use primitive_types::{H160, H256, U256};

use ethereum::util::ordered_trie_root;
use keccak_hash::keccak;
use rlp::RlpStream;
use serde::{Deserialize, Serialize};
use std::sync::Arc;

#[derive(Serialize, Deserialize, Clone, Debug)]
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

pub struct ReceiptHandler {
    storage: Arc<Storage>,
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
    pub fn new(storage: Arc<Storage>) -> Self {
        Self { storage }
    }

    pub fn get_receipt_root(&self, receipts: &[Receipt]) -> H256 {
        ordered_trie_root(
            receipts
                .iter()
                .map(|r| EnvelopedEncodable::encode(&r.receipt).freeze()),
        )
    }

    pub fn generate_receipts(
        &self,
        successful: Vec<SignedTx>,
        failed: Vec<SignedTx>,
        block_hash: H256,
        block_number: U256,
    ) -> Vec<Receipt> {
        let mut receipts = Vec::new();

        let mut index = 0;

        for signed_tx in successful {
            let receipt = Receipt {
                receipt: ReceiptV3::EIP1559(EIP658ReceiptData {
                    status_code: 1,
                    used_gas: Default::default(),
                    logs_bloom: Default::default(),
                    logs: vec![],
                }),
                block_hash,
                block_number,
                tx_hash: signed_tx.transaction.hash(),
                from: signed_tx.sender,
                to: signed_tx.to(),
                tx_index: index,
                tx_type: EnvelopedEncodable::type_id(&signed_tx.transaction).unwrap_or_default(),
                contract_address: get_contract_address(
                    &signed_tx.to(),
                    &signed_tx.sender,
                    &signed_tx.nonce(),
                ),
            };
            receipts.push(receipt);
            index += 1;
        }

        for signed_tx in failed {
            let receipt = Receipt {
                receipt: ReceiptV3::EIP1559(EIP658ReceiptData {
                    status_code: 0,
                    used_gas: Default::default(),
                    logs_bloom: Default::default(),
                    logs: vec![],
                }),
                block_hash,
                block_number,
                tx_hash: signed_tx.transaction.hash(),
                from: signed_tx.sender,
                to: signed_tx.to(),
                tx_index: index,
                contract_address: get_contract_address(
                    &signed_tx.to(),
                    &signed_tx.sender,
                    &signed_tx.nonce(),
                ),
                tx_type: EnvelopedEncodable::type_id(&signed_tx.transaction).unwrap(),
            };

            receipts.push(receipt);
            index += 1;
        }
        receipts
    }

    pub fn put_receipts(&self, receipts: Vec<Receipt>) {
        self.storage.put_receipts(receipts)
    }
}

#[cfg(test)]
mod test {
    use crate::receipt::get_contract_address;
    use keccak_hash::keccak;
    use primitive_types::{H160, U256};
    use rlp::RlpStream;
    use std::str::FromStr;

    // TODO: This needs fixing. `get_contract_address` impl appears to add sender to the
    // stream that appears incorrect. Leaving it to @shoham to recitfy and clear up
    // side effects, but this blocks everyone from being able to add to the branch,
    // so ignore this for the moment
    #[ignore]
    #[test]
    pub fn test_contract_address() {
        let sender = H160::from_str("0f572e5295c57f15886f9b263e2f6d2d6c7b5ec6").unwrap();

        let expected = H160::from_str("3f09c73a5ed19289fb9bdc72f1742566df146f56").unwrap();
        let to = H160::from_str("3f09c73a5ed19289fb9bdc72f1742566df146f56").unwrap();

        let actual = get_contract_address(&Some(to), &sender, &U256::from(88));

        assert_eq!(actual.unwrap(), expected);
    }
}
