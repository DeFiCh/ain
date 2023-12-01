use crate::database::db_manger::RocksDB;
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
    ) -> Result<(Vec<PoolSwapAggregated>)> {
        todo!()
    }
    pub async fn put(&self, aggregated: PoolSwapAggregated) -> Result<()> {
        todo!()
    }
    pub async fn get(&self, id: String) -> Result<PoolSwapAggregated> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
}
