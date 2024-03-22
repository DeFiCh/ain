use ain_evm::{bytes::Bytes, log::LogIndex};
use ethereum_types::{H160, H256, U256};
use serde_with::{serde_as, OneOrMany};

use crate::block::BlockNumber;

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct LogResult {
    // Consensus fields:
    // Address of the contract that generated the event
    pub address: H160,
    // List of topics provided by the contract
    pub topics: Vec<H256>,
    // Supplied by the contract, usually ABI-encoded
    pub data: Bytes,

    // Derived fields. These fields are filled in by the node
    // but not secured by consensus.
    // Block in which the transaction was included
    pub block_number: U256,
    // Hash of the transaction
    pub transaction_hash: H256,
    // Index of the transaction in the block
    pub transaction_index: U256,
    // Hash of the block in which the transaction was included
    pub block_hash: H256,
    // Index of the log in the block
    pub log_index: U256,

    // The removed field is true if this log was reverted due to a chain reorganization.
    // You must pay attention to this field if you receive logs through a filter query.
    pub removed: bool,
}

impl From<LogIndex> for LogResult {
    fn from(log: LogIndex) -> Self {
        Self {
            address: log.address,
            topics: log.topics,
            data: Bytes::from(log.data),
            block_number: log.block_number,
            transaction_hash: log.transaction_hash,
            transaction_index: log.transaction_index,
            block_hash: log.block_hash,
            log_index: log.log_index,
            removed: log.removed,
        }
    }
}

/// Call request
#[serde_as]
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct GetLogsRequest {
    #[serde_as(as = "Option<OneOrMany<_>>")]
    pub address: Option<Vec<H160>>,
    pub block_hash: Option<H256>,
    pub from_block: Option<BlockNumber>,
    pub to_block: Option<BlockNumber>,
    pub topics: Option<LogRequestTopics>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum LogRequestTopics {
    VecOfHashes(Vec<Option<H256>>),
    VecOfHashVecs(Vec<Vec<Option<H256>>>),
}
