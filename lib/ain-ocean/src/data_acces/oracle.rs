use anyhow::{anyhow, Error, Result};
use rocksdb::IteratorMode;
use serde::{Deserialize, Serialize};
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB,SortOrder},
    model::oracle::Oracle,
};

pub struct OracleDb {
    pub db: RocksDB,
}

impl OracleDb {
    pub async fn query(
        &self,
        limit: i32,
        lt: String,
        sort_order: SortOrder,
    ) -> Result<Vec<Oracle>> {
        let iterator = self.db.iterator("oracle", IteratorMode::End)?;
        let mut oracles: Vec<Oracle> = Vec::new();
        let collected_items: Vec<_> = iterator.collect();

        for result in collected_items.into_iter().rev() {
            let value = match result {
                Ok((_, value)) => value,
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let oracle: Oracle = serde_json::from_slice(&value)?;
            oracles.push(oracle);
            if oracles.len() as i32 >= limit {
                break;
            }
        }

        match sort_order {
            SortOrder::Ascending => oracles.sort_by(|a, b| a.id.cmp(&b.id)),
            SortOrder::Descending => oracles.sort_by(|a, b| b.id.cmp(&a.id)),
        }

        Ok(oracles)
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
