use bitcoin::BlockHash;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default, Clone, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Block {
    pub id: String,
    pub hash: String,
    pub previous_hash: String,
    pub height: u32,
    pub version: i32,
    pub time: u32,
    pub median_time: i64,
    pub transaction_count: usize,
    pub difficulty: u32,
    pub masternode: String,
    pub minter: String,
    pub minter_block_count: u64,
    pub stake_modifier: String,
    pub merkleroot: String,
    pub size: usize,
    pub size_stripped: usize,
    pub weight: i64,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct BlockContext {
    pub hash: BlockHash,
    pub height: u32,
    pub time: u64,
    pub median_time: u64,
}
