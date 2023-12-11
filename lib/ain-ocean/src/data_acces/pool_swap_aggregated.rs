use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::poolswap_aggregated::PoolSwapAggregated,
};

pub struct PoolSwapAggregatedDb {
    pub db: RocksDB,
}

impl PoolSwapAggregatedDb {
    pub async fn query(
        &self,
        key: String,
        limit: i32,
        lt: String,
    ) -> Result<(Vec<PoolSwapAggregated>)> {
        todo!()
    }
    pub async fn put(&self, aggregated: PoolSwapAggregated) -> Result<()> {
        match serde_json::to_string(&aggregated) {
            Ok(value) => {
                let key = aggregated.id.clone();
                self.db
                    .put("pool_swap_aggregated", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn get(&self, id: String) -> Result<PoolSwapAggregated> {
        match self.db.get("pool_swap_aggregated", id.as_bytes()) {
            Ok(Some(value)) => {
                let pool_swap: PoolSwapAggregated =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(pool_swap)
            }
            Ok(None) => Err(anyhow!("No data found for the given ID")),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("pool_swap_aggregated", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
