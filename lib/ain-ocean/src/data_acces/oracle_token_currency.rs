use crate::database::RocksDB;
use crate::model::oracle_token_currency::OracleTokenCurrency;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OracleTokenCurrencyDb {}

impl OracleHistoryDB {
    pub async fn query(&self, limit: i32, lt: String) -> Result(Vec<OracleTokenCurrency>) {}
    pub async fn put(&self, oracle_token: OracleTokenCurrency) -> Result() {}
    pub async fn delete(&self, id: String) -> Result() {}
}
