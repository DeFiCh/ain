use crate::block::BlockNumber;
use ain_evm::bytes::Bytes;
use ain_evm::log::LogIndex;
use primitive_types::{H160, H256, U256};

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct LogResult {
    pub block_hash: H256,
    pub log_index: U256,
    pub removed: bool,
    pub transaction_hash: H256,
    pub transaction_index: usize,
    pub address: H160,
    pub data: Bytes,
    pub topics: Vec<H256>,
}

impl From<LogIndex> for LogResult {
    fn from(log: LogIndex) -> Self {
        Self {
            block_hash: log.block_hash,
            log_index: log.log_index,
            removed: log.removed,
            transaction_hash: log.transaction_hash,
            transaction_index: log.transaction_index,
            address: log.address,
            data: Bytes::from(log.data),
            topics: log.topics,
        }
    }
}

/// Call request
#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct GetLogsRequest {
    pub address: Option<Vec<H160>>,
    pub block_hash: Option<H256>,
    pub from_block: Option<BlockNumber>,
    pub to_block: Option<BlockNumber>,
    pub topics: Option<Vec<H256>>,
}
