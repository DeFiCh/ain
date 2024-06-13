use std::collections::HashMap;

use bitcoin::BlockHash;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type PoolSwapAggregatedId = (u32, u32, BlockHash); // (pool_id, interval, block_hash)
pub type PoolSwapAggregatedKey = (u32, u32, i64); // (pool_id, interval, bucket)

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregated {
    pub id: String,
    pub key: String,
    pub bucket: i64,
    pub aggregated: PoolSwapAggregatedAggregated,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregatedAggregated {
    pub amounts: HashMap<String, String>, // amounts[tokenId] = BigNumber(volume)
}
