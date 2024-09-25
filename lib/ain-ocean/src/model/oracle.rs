use ain_dftx::{Currency, Token, Weightage};
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type Oracleid = Txid;
pub type OracleHistoryId = (Txid, u32); //oracleId-height

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Oracle {
    pub owner_address: String,
    pub weightage: Weightage,
    pub price_feeds: Vec<PriceFeed>,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceFeed {
    pub token: Token,
    pub currency: Currency,
}
