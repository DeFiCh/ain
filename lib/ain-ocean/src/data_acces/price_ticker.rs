use crate::database::db_manger::RocksDB;
use crate::model::price_ticker::PriceTicker;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OraclePriceAggregatedIntervalDb {}

impl OraclePriceAggregatedIntervalDb {
    pub async fn query(&self, limit: i32, lt: String) -> Result<Vec<PriceTicker>> {
        todo!()
    }
    pub async fn get(&self, id: String) -> Result<PriceTicker> {
        todo!()
    }
    pub async fn put(&self, price: PriceTicker) -> Result<()> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
}
