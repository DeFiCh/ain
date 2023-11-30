use crate::database::RocksDB;
use crate::model::poolswap_aggregated::PoolSwapAggregated;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct PoolSwapAggregatedDb {}

impl PoolSwapAggregatedDb {
    pub async fn query(
        &self,
        key: String,
        limit: i32,
        lt: String,
    ) -> Result(vec<PoolSwapAggregated>) {
    }
    pub async fn put(&self, aggregated: PoolSwapAggregated) -> Result() {}
    pub async fn get(&self, id: String) -> Result(PoolSwapAggregated) {}
    pub async fn delete(&self, id: String) -> Result() {}
}
