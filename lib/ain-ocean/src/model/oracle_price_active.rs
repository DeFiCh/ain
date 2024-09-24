use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type OraclePriceActiveId = (String, String, u32); //token-currency-height
pub type OraclePriceActiveKey = (String, String); //token-currency
#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActive {
    pub id: OraclePriceActiveId,
    pub key: OraclePriceActiveKey,
    pub sort: String, //height
    pub active: Option<OraclePriceActiveNext>,
    pub next: Option<OraclePriceActiveNext>,
    pub is_live: bool,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNext {
    #[serde(with = "rust_decimal::serde::str")]
    pub amount: Decimal,
    pub weightage: u32,
    pub oracles: OraclePriceActiveNextOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNextOracles {
    pub active: i32,
    pub total: i32,
}
