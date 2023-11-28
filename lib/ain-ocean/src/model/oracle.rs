use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct Oracle {
    pub id: String,
    pub owner_address: String,
    pub weightage: i32,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: OracleBlock,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PriceFeedsItem {
    pub token: String,
    pub currency: String,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OracleBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}
