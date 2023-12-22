use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::oracle_price_active::OraclePriceActive,
};

pub struct OraclePriceActiveDb {
    pub db: RocksDB,
}

impl OraclePriceActiveDb {
    pub async fn query(
        &self,
        oracle_id: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<OraclePriceActive>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let oracle_price_active: Result<Vec<_>> = self
            .db
            .iterator_by_id("oracle_price_active", &oracle_id, iter_mode)?
            .into_iter()
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let oracle_price: OraclePriceActive = serde_json::from_slice(&value)?;

                        Ok(oracle_price)
                    })
            })
            .collect();
        Ok(oracle_price_active?)
    }
    pub async fn put(&self, oracle_price_active: OraclePriceActive) -> Result<()> {
        match serde_json::to_string(&oracle_price_active) {
            Ok(value) => {
                let key = oracle_price_active.id.clone();
                self.db
                    .put("oracle_price_active", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("oracle_price_active", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
