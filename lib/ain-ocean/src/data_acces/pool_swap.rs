use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::poolswap::PoolSwap,
};

pub struct PoolSwapDb {
    pub db: RocksDB,
}

impl PoolSwapDb {
    pub async fn query(&self, key: String, limit: i32, lt: String) -> Result<Vec<PoolSwap>> {
        todo!()
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
