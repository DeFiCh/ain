use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct Block {
    pub id: String,
    pub hash: String,
    pub previous_hash: String,
    pub height: i32,
    pub version: i32,
    pub time: i32,
    pub median_time: i32,
    pub transaction_count: i32,
    pub difficulty: i32,
    pub masternode: String,
    pub minter: String,
    pub minter_block_count: i32,
    pub reward: String,
    pub stake_modifier: String,
    pub merkleroot: String,
    pub size: i32,
    pub size_stripped: i32,
    pub weight: i32,
}



