use anyhow::{anyhow, Result};
use rocksdb::{Direction, IteratorMode};
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::script_activity::ScriptActivity,
};

pub struct ScriptUnspentDB {
    pub db: RocksDB,
}

impl ScriptUnspentDB {
    pub async fn query(
        &self,
        hid: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<ScriptActivity>> {
        // let prefix = format!("script_activity_hid_sort:{}:", lt.unwrap_or_default());
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let script_activity: Result<Vec<_>> = self
            .db
            .iterator_by_id("script_activity", &hid, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let sa: ScriptActivity = serde_json::from_slice(&value)?;
                        Ok(sa)
                    })
            })
            .collect();
        Ok(script_activity?)
    }
    pub async fn store(&self, unspent: ScriptActivity) -> Result<()> {
        match serde_json::to_string(&unspent) {
            Ok(value) => {
                let key = unspent.hid.clone();
                self.db
                    .put("script_activity", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, hid: String) -> Result<()> {
        match self.db.delete("script_activity", hid.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
