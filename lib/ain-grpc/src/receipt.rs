use ain_evm::{bytes::Bytes, receipt::Receipt};
use ethereum::{EIP658ReceiptData, Log};
use ethereum_types::{H160, H256, U256};

use crate::logs::LogResult;

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
            effective_gas_price: b.effective_gas_price,
            from: b.from,
            gas_used: data.used_gas,
            logs: {
                data.logs
                    .into_iter()
                    .enumerate()
                    .map(
                        |(
                            log_index,
                            Log {
                                address,
                                topics,
                                data,
                            },
                        )| LogResult {
                            address,
                            topics,
                            data: Bytes::from(data),
                            block_number: b.block_number,
                            block_hash: b.block_hash,
                            transaction_hash: b.tx_hash,
                            transaction_index: b.tx_index.into(),
                            log_index: (b.logs_index + log_index).into(), // safe since we sub the total number of logs during receipt generation
                            removed: false,
                        },
                    )
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
