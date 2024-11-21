use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OraclePriceFeedId = (String, String, Txid, [u8; 4], Txid); // token-currency-oracle_id-height-txid

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceFeed {
    pub time: i32,
    pub amount: i64,
    pub block: BlockContext,
}
