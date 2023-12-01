use crate::database::db_manger::RocksDB;
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
    ) -> Result<(Vec<OraclePriceAggregated>)> {
        todo!()
    }
    pub async fn put(&self, oracle: OraclePriceAggregated) -> Result<()> {
        todo!()
    }
    pub async fn get(&self, id: String) -> Result<OraclePriceAggregated> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
}
