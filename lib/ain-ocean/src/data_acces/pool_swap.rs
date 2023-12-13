use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB, SortOrder},
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
        lt: String,
        sort_order: SortOrder,
    ) -> Result<Vec<PoolSwap>> {
        let iterator = self.db.iterator("pool_swap", IteratorMode::End)?;
        let mut pool_vin: Vec<PoolSwap> = Vec::new();
        let collected_blocks: Vec<_> = iterator.collect();

        for result in collected_blocks.into_iter().rev() {
            let (key, value) = match result {
                Ok((key, value)) => (key, value),
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let vin: PoolSwap = serde_json::from_slice(&value)?;
            if vin.id == id {
                pool_vin.push(vin);
                if pool_vin.len() as i32 >= limit {
                    break;
                }
            }
        }

        // Sort blocks based on the specified sort order
        match sort_order {
            SortOrder::Ascending => pool_vin.sort_by(|a, b| a.txid.cmp(&b.txid)),
            SortOrder::Descending => pool_vin.sort_by(|a, b| b.txid.cmp(&a.txid)),
        }

        Ok(pool_vin)
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
