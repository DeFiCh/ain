use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::poolswap::PoolSwap,
};

pub struct PoolSwapDb {
    pub db: RocksDB,
}

impl PoolSwapDb {
    pub async fn query(
        &self,
        id: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<PoolSwap>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let pool_swap: Result<Vec<_>> = self
            .db
            .iterator_by_id("pool_swap", &id, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let pool: PoolSwap = serde_json::from_slice(&value)?;

                        Ok(pool)
                    })
            })
            .collect();
        Ok(pool_swap?)
    }
    pub async fn put(&self, swap: PoolSwap) -> Result<()> {
        match serde_json::to_string(&swap) {
            Ok(value) => {
                let key = swap.id.clone();
                self.db.put("pool_swap", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("pool_swap", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
