use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
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
    pub block: OraclePriceFeedBlock,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceFeedBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}
