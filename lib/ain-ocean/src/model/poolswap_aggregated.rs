use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregated {
    pub id: String,
    pub key: String,
    pub bucket: i32,
    pub aggregated: PoolSwapAggregatedAggregated,
    pub block: PoolSwapAggregatedBlock,
}


#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregatedAggregated {
    pub amounts: Vec<String>,
}


#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregatedBlock {
    pub median_time: i32,
}



