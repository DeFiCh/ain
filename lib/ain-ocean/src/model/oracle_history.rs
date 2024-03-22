use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OracleHistory {
    pub id: String,
    pub oracle_id: String,
    pub sort: String,
    pub owner_address: String,
    pub weightage: i32,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PriceFeedsItem {
    pub token: String,
    pub currency: String,
}
