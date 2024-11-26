use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type OraclePriceActiveId = (String, String, [u8; 4]); //token-currency-height

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActive {
    pub active: Option<OraclePriceActiveNext>,
    pub next: Option<OraclePriceActiveNext>,
    pub is_live: bool,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNext {
    #[serde(with = "rust_decimal::serde::str")]
    pub amount: Decimal, // aggregated_amount
    #[serde(with = "rust_decimal::serde::str")]
    pub weightage: Decimal, // aggregated_weightage
    pub oracles: OraclePriceActiveNextOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceActiveNextOracles {
    #[serde(with = "rust_decimal::serde::str")]
    pub active: Decimal, // aggregated_count
    pub total: i32,
}
