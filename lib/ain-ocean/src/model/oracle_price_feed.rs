use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OraclePriceFeedId = (String, String, Txid, Txid); // token-currency-oracle_id-txid

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceFeed {
    pub time: i32,
    pub amount: i64,
    pub block: BlockContext,
}
