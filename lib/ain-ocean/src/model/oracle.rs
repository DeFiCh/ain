use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::{ApiResponseOraclePriceFeed, BlockContext, OraclePriceFeed};

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct Oracle {
    pub id: Txid,
    pub owner_address: String,
    pub weightage: u8,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceFeedsItem {
    pub token: String,
    pub currency: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PriceOracles {
    pub id: String,
    pub key: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub weightage: u8,
    pub feed: Option<ApiResponseOraclePriceFeed>,
    pub block: BlockContext,
}
