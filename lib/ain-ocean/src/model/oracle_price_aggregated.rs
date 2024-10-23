use serde::{Deserialize, Serialize};

use super::{BlockContext, OraclePriceActiveNext};

pub type OraclePriceAggregatedId = (String, String, u32); //token-currency-height

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregated {
    pub aggregated: OraclePriceActiveNext,
    pub block: BlockContext,
}
