use crate::database::RocksDB;
use crate::model::oracle::Oracle;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OracleDb {}

impl OracleHistoryDB {
    pub async fn query(&self, limit: i32, lt: String) -> Result(Oracle) {}
    pub async fn put(&self, oracle: Oracle) -> Result() {}
    pub async fn get(&self, id: String) -> Result(Oracle) {}
    pub async fn delete(&self, id: String) -> Result() {}
}
