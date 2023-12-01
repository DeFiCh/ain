use crate::database::db_manger::RocksDB;
use crate::model::oracle_price_aggregated_interval::OraclePriceAggregatedInterval;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OraclePriceAggregatedIntervalDb {}

impl OraclePriceAggregatedIntervalDb {
    pub async fn query(
        &self,
        key: String,
        limit: i32,
        lt: String,
    ) -> Result<Vec<OraclePriceAggregatedInterval>> {
        todo!()
    }
    pub async fn put(&self, oracle: OraclePriceAggregatedInterval) -> Result<()> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
}
