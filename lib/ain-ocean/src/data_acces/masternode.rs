use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::masternode::Masternode;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct MasterNodeDB {
    pub db: RocksDB,
}

impl MasterNodeDB {
    pub async fn query(&self, limit: i32, lt: i32) -> Result<Vec<Masternode>> {
        todo!()
    }
    pub async fn get(&self, id: String) -> Result<Option<Masternode>> {
        match self.db.get("masternode", id.as_bytes()) {
            Ok(Some(value)) => {
                let master_node: Masternode =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(master_node))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn store(&self, stats: Masternode) -> Result<()> {
        match serde_json::to_string(&stats) {
            Ok(value) => {
                let key = stats.id.clone();
                self.db
                    .put("masternode", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("masternode", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
