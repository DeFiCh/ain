use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::oracle_token_currency::OracleTokenCurrency,
};

pub struct OracleTokenCurrencyDb {
    pub db: RocksDB,
}

impl OracleTokenCurrencyDb {
    pub async fn query(
        &self,
        oracle_id: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<OracleTokenCurrency>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let oracle_token_price: Result<Vec<_>> = self
            .db
            .iterator_by_id("oracle_token_currency", &oracle_id, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let oracle_token: OracleTokenCurrency = serde_json::from_slice(&value)?;
                        Ok(oracle_token)
                    })
            })
            .collect();
        Ok(oracle_token_price?)
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
