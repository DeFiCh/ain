use anyhow::{anyhow, Error, Result};
use rocksdb::IteratorMode;
use serde::{Deserialize, Serialize};
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::oracle::Oracle,
};

pub struct OracleDb {
    pub db: RocksDB,
}

impl OracleDb {
    pub async fn query(
        &self,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<Oracle>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let oracle: Result<Vec<_>> = self
            .db
            .iterator("oracle", iter_mode)?
            .into_iter()
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let _oracle: Oracle = serde_json::from_slice(&value)?;

                        Ok(_oracle)
                    })
            })
            .collect();
        Ok(oracle?)
    }

    pub async fn store(&self, oracle: Oracle) -> Result<()> {
        match serde_json::to_string(&oracle) {
            Ok(value) => {
                let key = oracle.id.clone();
                self.db.put("oracle", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn get(&self, id: String) -> Result<Option<Oracle>> {
        match self.db.get("oracle", id.as_bytes()) {
            Ok(Some(value)) => {
                let oracle: Oracle = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(oracle))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("oracle", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
