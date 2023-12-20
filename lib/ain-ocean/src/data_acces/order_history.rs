use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB, SortOrder},
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
        lt: String,
        sort_order: SortOrder,
    ) -> Result<Vec<OracleHistory>> {
        let iterator = self.db.iterator("oracle_history", IteratorMode::End)?;
        let mut oracle_history: Vec<OracleHistory> = Vec::new();
        let collected_blocks: Vec<_> = iterator.collect();

        for result in collected_blocks.into_iter().rev() {
            let (key, value) = match result {
                Ok((key, value)) => (key, value),
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let oracle: OracleHistory = serde_json::from_slice(&value)?;
            if oracle.id == oracle_id {
                oracle_history.push(oracle);
                if oracle_history.len() as i32 >= limit {
                    break;
                }
            }
        }

        // Sort blocks based on the specified sort order
        match sort_order {
            SortOrder::Ascending => oracle_history.sort_by(|a, b| a.oracle_id.cmp(&b.oracle_id)),
            SortOrder::Descending => oracle_history.sort_by(|a, b| b.oracle_id.cmp(&a.oracle_id)),
        }

        Ok(oracle_history)
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
