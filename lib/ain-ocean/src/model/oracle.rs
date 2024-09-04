use ain_dftx::{Currency, Token, Weightage};
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Oracle {
    pub id: Txid,
    pub owner_address: String,
    pub weightage: Weightage,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceFeedsItem {
    pub token: Token,
    pub currency: Currency,
}
