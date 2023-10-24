use std::sync::Arc;

use anyhow::format_err;
use ethereum::{util::ordered_trie_root, EnvelopedEncodable, ReceiptV3};
use ethereum_types::{H160, H256, U256};
use keccak_hash::keccak;
use rlp::RlpStream;
use serde::{Deserialize, Serialize};

use crate::{
    blocktemplate::ReceiptAndOptionalContractAddress,
    storage::{traits::ReceiptStorage, Storage},
    transaction::SignedTx,
    Result,
};

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
    pub logs_index: usize,
    pub cumulative_gas: U256,
    pub effective_gas_price: U256,
}

pub struct ReceiptService {
    storage: Arc<Storage>,
}

fn get_contract_address(sender: &H160, nonce: &U256) -> H160 {
    let mut stream = RlpStream::new_list(2);
    stream.append(sender);
    stream.append(nonce);

    H160::from(keccak(stream.as_raw()))
}

impl ReceiptService {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self { storage }
    }

    pub fn get_receipts_root(receipts: &[ReceiptAndOptionalContractAddress]) -> H256 {
        ordered_trie_root(
            receipts
                .iter()
                .map(|(r, _)| EnvelopedEncodable::encode(r).freeze()),
        )
    }

    pub fn generate_receipts(
        &self,
        transactions: &[Box<SignedTx>],
        receipts_and_contract_address: Vec<ReceiptAndOptionalContractAddress>,
        block_hash: H256,
        block_number: U256,
        base_fee: U256,
    ) -> Result<Vec<Receipt>> {
        let mut logs_size = 0_usize;
        let mut cumulative_gas = U256::zero();

        transactions
            .iter()
            .enumerate()
            .zip(receipts_and_contract_address)
            .map(|((index, signed_tx), (receipt, contract_address))| {
                let receipt_data = match &receipt {
                    ReceiptV3::Legacy(data)
                    | ReceiptV3::EIP2930(data)
                    | ReceiptV3::EIP1559(data) => data,
                };
                let logs_len = receipt_data.logs.len();
                logs_size = logs_size
                    .checked_add(logs_len)
                    .ok_or_else(|| format_err!("logs_size overflow"))?;
                cumulative_gas = cumulative_gas
                    .checked_add(receipt_data.used_gas)
                    .ok_or_else(|| format_err!("cumulative_gas overflow"))?;

                let receipt = Receipt {
                    receipt,
                    block_hash,
                    block_number,
                    tx_hash: signed_tx.hash(),
                    from: signed_tx.sender,
                    to: signed_tx.to(),
                    tx_index: index,
                    tx_type: signed_tx.transaction.type_id().unwrap_or_default(),
                    contract_address: signed_tx.to().is_none().then(|| {
                        contract_address.unwrap_or_else(|| {
                            get_contract_address(&signed_tx.sender, &signed_tx.nonce())
                        })
                    }),
                    logs_index: logs_size
                        .checked_sub(logs_len)
                        .ok_or_else(|| format_err!("logs_size underflow"))?,
                    cumulative_gas,
                    effective_gas_price: signed_tx.effective_gas_price(base_fee)?,
                };

                Ok(receipt)
            })
            .collect()
    }

    pub fn put_receipts(&self, receipts: Vec<Receipt>) -> Result<()> {
        self.storage.put_receipts(receipts)
    }
}

#[cfg(test)]
mod test {
    use std::str::FromStr;

    use ethereum_types::{H160, U256};

    use crate::receipt::get_contract_address;

    #[test]
    pub fn test_contract_address() {
        let sender = H160::from_str("0f572e5295c57f15886f9b263e2f6d2d6c7b5ec6").unwrap();
        let expected = H160::from_str("3f09c73a5ed19289fb9bdc72f1742566df146f56").unwrap();

        let actual = get_contract_address(&sender, &U256::from(88));

        assert_eq!(actual, expected);
    }
}
