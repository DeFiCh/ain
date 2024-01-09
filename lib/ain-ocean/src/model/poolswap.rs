use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwap {
    pub id: String,
    pub txid: String,
    pub txno: i32,
    pub pool_pair_id: String,
    pub sort: String,
    pub from_amount: String,
    pub from_token_id: i32,
    pub block: BlockContext,
}
