use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::oracle_token_currency::OracleTokenCurrency;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OracleTokenCurrencyDb {
    pub db: RocksDB,
}

impl OracleTokenCurrencyDb {
    pub async fn query(&self, limit: i32, lt: String) -> Result<Vec<OracleTokenCurrency>> {
        todo!()
    }
    pub async fn put(&self, oracle_token: OracleTokenCurrency) -> Result<()> {
        match serde_json::to_string(&oracle_token) {
            Ok(value) => {
                let key = oracle_token.id.clone();
                self.db
                    .put("oracle_token_currency", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("oracle_token_currency", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
