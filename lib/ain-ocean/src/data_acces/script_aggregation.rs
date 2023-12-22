use anyhow::{anyhow, Result};
use rocksdb::{Direction, IteratorMode};
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::script_aggregation::ScriptAggregation,
};

pub struct ScriptAggretionDB {
    pub db: RocksDB,
}

impl ScriptAggretionDB {
    pub async fn query(
        &self,
        hid: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<ScriptAggregation>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let script_activity: Result<Vec<_>> = self
            .db
            .iterator_by_id("script_aggregation", &hid, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let sa: ScriptAggregation = serde_json::from_slice(&value)?;
                        Ok(sa)
                    })
            })
            .collect();
        Ok(script_activity?)
    }
    pub async fn store(&self, aggregation: ScriptAggregation) -> Result<()> {
        match serde_json::to_string(&aggregation) {
            Ok(value) => {
                let key = aggregation.hid.clone();
                self.db
                    .put("script_aggregation", key.as_bytes(), value.as_bytes())?;

                let h = aggregation.block.height.clone();
                let height: &[u8] = &h.to_be_bytes();
                self.db
                    .put("script_aggregation_mapper", height, key.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn get(&self, hid: String) -> Result<Option<ScriptAggregation>> {
        match self.db.get("script_aggregation", hid.as_bytes()) {
            Ok(Some(value)) => {
                let oracle: ScriptAggregation =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(oracle))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, hid: String) -> Result<()> {
        match self.db.delete("script_aggregation", hid.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
