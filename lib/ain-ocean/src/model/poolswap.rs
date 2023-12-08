use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwap {
    pub id: String,
    pub txid: String,
    pub txno: i32,
    pub pool_pair_id: String,
    pub sort: String,
    pub from_amount: String,
    pub from_token_id: i32,
    pub block: PoolSwapBlock,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}
