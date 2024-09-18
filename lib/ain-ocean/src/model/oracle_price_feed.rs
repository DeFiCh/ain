use ain_dftx::{Currency, Token};
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OraclePriceFeedId = (String, String, Txid, Txid); // token-currency-oracle_id-txid
pub type OraclePriceFeedkey = (String, String, Txid); // token-currency-oracle_id

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceFeed {
    pub token: Token,
    pub currency: Currency,
    pub oracle_id: Txid,
    pub txid: Txid,
    pub time: i32,
    pub amount: i64,
    pub block: BlockContext,
}
