use std::collections::HashMap;

use bitcoin::BlockHash;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type PoolSwapAggregatedId = (u32, u32, BlockHash); // (pool_id, interval, block_hash)
pub type PoolSwapAggregatedKey = (u32, u32, i64); // (pool_id, interval, bucket) bucket is for next page query

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregated {
    pub bucket: i64,
    pub aggregated: PoolSwapAggregatedAggregated,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapAggregatedAggregated {
    pub amounts: HashMap<u64, String>, // amounts[tokenId] = BigNumber(volume)
}
