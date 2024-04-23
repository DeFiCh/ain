use std::{fmt, str::FromStr};

use bitcoin::Txid;
use serde::{Deserialize, Serialize};
use serde_with::{serde_as, DisplayFromStr};

use super::BlockContext;
pub type OraclePriceFeedId = (String, String, Txid, Txid); // token-currency-oracle_id-txid
pub type OraclePriceFeedkey = (String, String, Txid); // token-currency-oracle_id

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceFeed {
    pub id: OraclePriceFeedId,
    pub key: OraclePriceFeedkey,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: Txid,
    pub txid: Txid,
    pub time: i32,
    pub amount: i64,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct ApiResponseOraclePriceFeed {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: Txid,
    pub txid: Txid,
    pub time: i32,
    pub amount: String,
    pub block: BlockContext,
}
