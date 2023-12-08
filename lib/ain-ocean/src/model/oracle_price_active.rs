use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActive {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub active: OraclePriceActiveActive,
    pub next: OraclePriceActiveNext,
    pub is_live: bool,
    pub block: OraclePriceActiveBlock,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveActive {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceActiveActiveOracles,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNext {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceActiveNextOracles,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveActiveOracles {
    pub active: i32,
    pub total: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNextOracles {
    pub active: i32,
    pub total: i32,
}
