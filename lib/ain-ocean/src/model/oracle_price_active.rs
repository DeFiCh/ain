use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type OraclePriceActiveId = (String,String,u32); //token-currency-height
pub type OraclePriceActiveKey = (String,String); //token-currency
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActive {
    pub id: OraclePriceActiveId,
    pub key: OraclePriceActiveKey,
    pub sort: String,    //height
    pub active: OraclePriceActiveActive,
    pub next: OraclePriceActiveNext,
    pub is_live: bool,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveActive {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceActiveActiveOracles,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNext {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceActiveNextOracles,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveActiveOracles {
    pub active: i32,
    pub total: i32,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNextOracles {
    pub active: i32,
    pub total: i32,
}
