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
    pub async fn query(
        &self,
        hid: String,
        limit: i32,
        gt: Option<String>,
    ) -> Result<Vec<ScriptUnspent>> {
        let prefix = format!("script_unspent_hid_sort:{}:", gt.unwrap_or_default());
        let iterator_result = self.db.iterator(
            "script_unspent",
            IteratorMode::From(prefix.as_bytes(), Direction::Forward),
        )?;
        let mut results = Vec::new();
        for item in iterator_result {
            match item {
                Ok((key, value)) => {
                    let key_str = String::from_utf8_lossy(&key);
                    if !key_str.starts_with(&prefix) {
                        break;
                    }

                    let script_unspent: ScriptUnspent = serde_json::from_slice(&value)?;

                    results.push(script_unspent);

                    if results.len() >= limit as usize {
                        break;
                    }
                }
                Err(err) => {
                    eprintln!("Error iterating over the database: {:?}", err);
                    return Err(err.into());
                }
            }
        }
        Ok(results)
    }
    pub async fn store(&self, unspent: ScriptUnspent) -> Result<()> {
        match serde_json::to_string(&unspent) {
            Ok(value) => {
                let key = unspent.hid.clone();
                self.db
                    .put("script_unspent", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, hid: String) -> Result<()> {
        match self.db.delete("script_unspent", hid.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
