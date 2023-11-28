use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedInterval {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceAggregatedIntervalAggregated,
    pub block: OraclePriceAggregatedIntervalBlock,
}

#[derive(Debug, Default)]
#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregated {
    pub amount: String,
    pub weightage: i32,
    pub count: i32,
    pub oracles: OraclePriceAggregatedIntervalAggregatedOracles,
}

#[derive(Debug, Default)]
#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Debug, Default)]
#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregatedOracles {
    pub active: i32,
    pub total: i32,
}
