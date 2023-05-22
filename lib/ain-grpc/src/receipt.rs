use ain_evm::receipt::Receipt;
use ethereum::EIP658ReceiptData;
use primitive_types::{H160, H256, U256};

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct LogResult {
    pub address: H160,
    pub topics: Vec<H256>,
    pub data: String,
    pub block_number: U256,
    pub block_hash: H256,
    pub transaction_hash: H256,
    pub transaction_index: String,
    pub log_index: String,
    pub removed: bool,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ReceiptResult {
    pub block_hash: H256,
    pub block_number: U256,
    pub contract_address: Option<H160>,
    pub cumulative_gas_used: U256,
    pub effective_gas_price: U256,
    pub from: H160,
    pub gas_used: U256,
    pub logs: Vec<LogResult>,
    pub logs_bloom: String,
    pub status: String,
    pub to: Option<H160>,
    pub transaction_hash: H256,
    pub transaction_index: String,
    pub r#type: String,
}

impl From<Receipt> for ReceiptResult {
    fn from(b: Receipt) -> Self {
        let data = EIP658ReceiptData::from(b.receipt);
        ReceiptResult {
            block_hash: b.block_hash,
            block_number: b.block_number,
            contract_address: b.contract_address,
            cumulative_gas_used: b.cumulative_gas,
            effective_gas_price: Default::default(),
            from: b.from,
            gas_used: data.used_gas,
            logs: {
                data.logs
                    .iter()
                    .enumerate()
                    .map(|(log_index, x)| LogResult {
                        address: x.clone().address,
                        topics: x.clone().topics,
                        data: format!("{:#x?}", x.data.to_ascii_lowercase()),
                        block_number: b.block_number,
                        block_hash: b.block_hash,
                        transaction_hash: b.tx_hash,
                        transaction_index: format!("{:#x}", b.tx_index),
                        log_index: { format!("{:#x}", b.logs_index + log_index) },
                        removed: false,
                    })
                    .collect::<Vec<LogResult>>()
            },
            logs_bloom: format!("{:#x}", data.logs_bloom),
            status: format!("{:#x}", data.status_code),
            to: b.to,
            transaction_hash: b.tx_hash,
            transaction_index: format!("{:#x}", b.tx_index),
            r#type: format!("{:#x}", b.tx_type),
        }
    }
}
