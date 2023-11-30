use crate::database::RocksDB;
use crate::model::poolswap::PoolSwap;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct PoolSwapDb {}

impl PoolSwapDb {
    pub async fn query(&self, key: String, limit: i32, lt: String) -> Result(vec<PoolSwap>) {}
    pub async fn put(&self, feed: PoolSwap) -> Result() {}
    pub async fn delete(&self, id: String) -> Result() {}
}
