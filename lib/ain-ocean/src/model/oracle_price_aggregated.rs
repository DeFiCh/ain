use serde::{Deserialize, Serialize};

use super::{BlockContext, OraclePriceActiveNext};

pub type OraclePriceAggregatedId = (String, String, i64, u32); //token-currency-mediantime-height

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregated {
    pub aggregated: OraclePriceActiveNext,
    pub block: BlockContext,
}
