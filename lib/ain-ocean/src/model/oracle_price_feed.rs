use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type OraclePriceFeedKey = (u32, u32, usize);
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceFeed {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub txid: String,
    pub time: i32,
    pub amount: String,
    pub block: BlockContext,
}
