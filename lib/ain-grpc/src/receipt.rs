use ethereum::{EIP658ReceiptData, Log};
use primitive_types::{H160, H256, U256};
use ain_evm::receipt::Receipt;
use std::error::Error;

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ReceiptResult {
    pub block_hash: H256,
    pub block_number: U256,
    pub contract_address: H160,
    pub cumulative_gas_used: U256,
    pub effective_gas_price: U256,
    pub from: H160,
    pub gas_used: U256,
    pub logs: Vec<Log>,
    pub logs_bloom: String,
    pub status: u8,
    pub to: H160,
    pub transaction_hash: H256,
    pub transaction_index: usize,
    pub typ: u8
}

impl From<Receipt> for ReceiptResult {
    fn from(b: Receipt) -> Self {
        let data = EIP658ReceiptData::from(b.receipt);
        ReceiptResult {
            block_hash: b.block_hash,
            block_number: b.block_number,
            contract_address: Default::default(),
            cumulative_gas_used: Default::default(),
            effective_gas_price: Default::default(),
            from: b.from,
            gas_used: data.used_gas,
            logs: data.logs,
            logs_bloom: format!("{:#x}", data.logs_bloom),
            status: data.status_code,
            to: b.to,
            transaction_hash: b.tx_hash,
            transaction_index: b.tx_index,
            typ: b.tx_type,
        }
    }
}

