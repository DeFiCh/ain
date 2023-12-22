use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::oracle_history::OracleHistory,
};

pub struct OracleHistoryDB {
    pub db: RocksDB,
}

impl OracleHistoryDB {
    pub async fn query(
        &self,
        oracle_id: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<OracleHistory>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let oracle_history: Result<Vec<_>> = self
            .db
            .iterator_by_id("oracle_history", &oracle_id, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let oracle: OracleHistory = serde_json::from_slice(&value)?;
                        Ok(oracle)
                    })
            })
            .collect();
        Ok(oracle_history?)
    }

    pub async fn store(&self, oracle_history: OracleHistory) -> Result<()> {
        match serde_json::to_string(&oracle_history) {
            Ok(value) => {
                let key = oracle_history.oracle_id.clone();
                self.db
                    .put("oracle_history", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, oracle_id: String) -> Result<()> {
        match self.db.delete("oracle_history", oracle_id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
