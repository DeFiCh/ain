use anyhow::{anyhow, Error, Result};
use serde::{Deserialize, Serialize};
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::script_unspent::ScriptUnspent,
};

pub struct ScriptUnspentDB {
    pub db: RocksDB,
}

impl ScriptUnspentDB {
    pub async fn query(&self, limit: i32, lt: String) -> Result<Vec<ScriptUnspent>> {
        todo!()
    }
    pub async fn store(&self, unspent: ScriptUnspent) -> Result<()> {
        match serde_json::to_string(&unspent) {
            Ok(value) => {
                let key = unspent.id.clone();
                self.db
                    .put("script_unspent", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("script_unspent", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
