use serde::{Deserialize, Serialize};

use super::{BlockContext, OraclePriceFeed};

#[derive(Debug, Serialize, Deserialize)]
pub struct PriceOracles {
    pub id: String,
    pub key: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub feed: OraclePriceFeed,
    pub block: BlockContext,
}
