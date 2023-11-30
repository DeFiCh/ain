use crate::database::RocksDB;
use crate::model::price_ticker::PriceTicker;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OraclePriceAggregatedIntervalDb {}

impl OraclePriceAggregatedIntervalDb {
    pub async fn query(&self, limit: i32, lt: String) -> Result(vec<PriceTicker>) {}
    pub async fn get(&self, id: String) -> Result(PriceTicker) {}
    pub async fn put(&self, price: PriceTicker) -> Result() {}
    pub async fn delete(&self, id: String) -> Result() {}
}
