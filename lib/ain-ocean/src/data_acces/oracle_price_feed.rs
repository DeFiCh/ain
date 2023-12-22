use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::oracle_price_feed::OraclePriceFeed,
};
use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

pub struct OraclePriceFeedDb {
    pub db: RocksDB,
}

impl OraclePriceFeedDb {
    pub async fn query(
        &self,
        oracle_id: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<OraclePriceFeed>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let oracle_price: Result<Vec<_>> = self
            .db
            .iterator_by_id("oracle_price_feed", &oracle_id, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let oracle_price_feed: OraclePriceFeed = serde_json::from_slice(&value)?;

                        Ok(oracle_price_feed)
                    })
            })
            .collect();
        Ok(oracle_price?)
    }
    pub async fn put(&self, oracle_price_feed: OraclePriceFeed) -> Result<()> {
        match serde_json::to_string(&oracle_price_feed) {
            Ok(value) => {
                let key = oracle_price_feed.id.clone();
                self.db
                    .put("oracle_price_feed", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("oracle_price_feed", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
