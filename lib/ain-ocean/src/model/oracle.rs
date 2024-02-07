use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct Oracle {
    pub id: Txid,
    pub owner_address: String,
    pub weightage: u8,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PriceFeedsItem {
    pub token: String,
    pub currency: String,
}
