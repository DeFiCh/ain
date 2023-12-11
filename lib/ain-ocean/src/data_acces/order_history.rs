use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::oracle_history::OracleHistory,
};

pub struct OracleHistoryDB {
    pub db: RocksDB,
}

impl OracleHistoryDB {
    pub async fn query(&self, oracleId: String, limit: i32, lt: String) -> Result<OracleHistory> {
        todo!()
    }

    pub async fn store(&self, oracle_history: OracleHistory) -> Result<()> {
        match serde_json::to_string(&oracle_history) {
            Ok(value) => {
                let key = oracle_history.id.clone();
                self.db
                    .put("oracle_history", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("oracle_history", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
