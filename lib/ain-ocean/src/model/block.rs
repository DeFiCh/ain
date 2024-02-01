use bitcoin::BlockHash;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Block {
    pub hash: BlockHash,
    pub previous_hash: Option<BlockHash>,
    pub height: u32,
    pub version: i32,
    pub time: i64,
    pub median_time: i64,
    pub transaction_count: usize,
    pub difficulty: f64,
    pub masternode: Option<String>,
    pub minter: Option<String>,
    pub minter_block_count: u64,
    pub stake_modifier: String,
    pub merkleroot: String,
    pub size: u64,
    pub size_stripped: u64,
    pub weight: u64,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct BlockContext {
    pub hash: BlockHash,
    pub height: u32,
    pub time: i64,
    pub median_time: i64,
}
