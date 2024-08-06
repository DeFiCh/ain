use serde::{Deserialize, Serialize};
use rust_decimal::Decimal;

use super::{BlockContext, OraclePriceActiveNext};

pub type OraclePriceAggregatedId = (String, String, u32); //token-currency-height
pub type OraclePriceAggregatedKey = (String, String); //token-currency

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregated {
    pub id: OraclePriceAggregatedId,
    pub key: OraclePriceAggregatedKey,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceActiveNext,
    pub block: BlockContext,
}
