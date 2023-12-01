use crate::database::db_manger::RocksDB;
use crate::model::oracle_history::OracleHistory;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OracleHistoryDB {}

impl OracleHistoryDB {
    pub async fn query(&self, oracleId: String, limit: i32, lt: String) -> Result<OracleHistory> {
        todo!()
    }
    pub async fn put(&self, oracleHistory: OracleHistory) -> Result<()> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
}
