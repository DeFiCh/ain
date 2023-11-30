use crate::database::RocksDB;
use crate::model::oracle_price_aggregated::OraclePriceAggregated;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OraclePriceAggrigatedDb {}

impl OraclePriceAggrigatedDb {
    pub async fn query(
        &self,
        key: String,
        limit: i32,
        lt: String,
    ) -> Result(vec<OraclePriceAggregated>) {
    }
    pub async fn put(&self, oracle: OraclePriceAggregated) -> Result() {}
    pub async fn get(&self, id: String) -> Result(OraclePriceAggregated) {}
    pub async fn delete(&self, id: String) -> Result() {}
}
