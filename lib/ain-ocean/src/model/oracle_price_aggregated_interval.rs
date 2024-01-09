use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedInterval {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceAggregatedIntervalAggregated,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregated {
    pub amount: String,
    pub weightage: i32,
    pub count: i32,
    pub oracles: OraclePriceAggregatedIntervalAggregatedOracles,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregatedOracles {
    pub active: i32,
    pub total: i32,
}
