use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OracleHistory {
    pub id: String,
    pub oracle_id: String,
    pub sort: String,
    pub owner_address: String,
    pub weightage: i32,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: OracleHistoryBlock,
}


#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PriceFeedsItem {
    pub token: String,
    pub currency: String,
}


#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OracleHistoryBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}



