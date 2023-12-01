use crate::database::db_manger::RocksDB;
use crate::model::oracle_token_currency::OracleTokenCurrency;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OracleTokenCurrencyDb {}

impl OracleTokenCurrencyDb {
    pub async fn query(&self, limit: i32, lt: String) -> Result<Vec<OracleTokenCurrency>> {
        todo!()
    }
    pub async fn put(&self, oracle_token: OracleTokenCurrency) -> Result<()> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
}
