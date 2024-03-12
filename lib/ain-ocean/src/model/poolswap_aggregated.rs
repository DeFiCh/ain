use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregated {
    pub id: String,
    pub key: String,
    pub bucket: i32,
    pub aggregated: PoolSwapAggregatedAggregated,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregatedAggregated {
    pub amounts: Vec<String>,
}
