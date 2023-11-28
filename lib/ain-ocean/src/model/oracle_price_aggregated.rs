use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregated {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceAggregatedAggregated,
    pub block: OraclePriceAggregatedBlock,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedAggregated {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceAggregatedAggregatedOracles,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedAggregatedOracles {
    pub active: i32,
    pub total: i32,
}
