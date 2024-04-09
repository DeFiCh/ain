use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
use crate::model::oracle::PriceFeedsItem;

pub type OracleHistoryId = (Txid, u32, Txid); //oracleId-height-txid

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OracleHistory {
    pub id: OracleHistoryId,
    pub oracle_id: Txid,
    pub sort: String, // height-txid
    pub owner_address: String,
    pub weightage: u8,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: BlockContext,
}
