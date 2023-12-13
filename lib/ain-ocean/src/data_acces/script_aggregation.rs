use anyhow::{anyhow, Error, Result};
use rocksdb::{Direction, IteratorMode};
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
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
        lt: String,
        sort_order: SortOrder,
    ) -> Result<Vec<ScriptAggregation>> {
        let iterator = self.db.iterator(
            "script_aggregation",
            IteratorMode::From(lt.as_bytes(), Direction::Reverse),
        )?;

        let mut script_aggre: Vec<ScriptAggregation> = Vec::new();

        for item in iterator {
            match item {
                Ok((key, value)) => {
                    let key_str = String::from_utf8_lossy(&key);

                    if !key_str.starts_with(&hid) {
                        break;
                    }

                    let vout: ScriptAggregation = serde_json::from_slice(&value)?;
                    script_aggre.push(vout);

                    if script_aggre.len() >= limit as usize {
                        break;
                    }
                }
                Err(err) => {
                    eprintln!("Error iterating over the database: {:?}", err);
                    return Err(err.into());
                }
            }
        }

        // Sorting based on the SortOrder
        match sort_order {
            SortOrder::Ascending => script_aggre.sort_by(|a, b| a.id.cmp(&b.id)),
            SortOrder::Descending => script_aggre.sort_by(|a, b| b.id.cmp(&a.id)),
        }

        Ok(script_aggre)
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
