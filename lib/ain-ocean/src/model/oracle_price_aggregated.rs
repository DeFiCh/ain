use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OraclePriceAggregatedId = (String, String, u32); //token-currency-height
pub type OraclePriceAggregatedKey = (String, String); //token-currency
#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregated {
    pub id: OraclePriceAggregatedId,
    pub key: OraclePriceAggregatedKey,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceAggregatedAggregated,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedAggregated {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceAggregatedAggregatedOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedAggregatedOracles {
    pub active: i32,
    pub total: i32,
}
