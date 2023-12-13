use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::{RocksDB, SortOrder};
use crate::model::oracle_price_feed::OraclePriceFeed;
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
        lt: String,
        sort_order: SortOrder,
    ) -> Result<Vec<OraclePriceFeed>> {
        let iterator = self.db.iterator("oracle_price_feed", IteratorMode::End)?;
        let mut oracle_price_feed: Vec<OraclePriceFeed> = Vec::new();
        let collected_blocks: Vec<_> = iterator.collect();

        for result in collected_blocks.into_iter().rev() {
            let (key, value) = match result {
                Ok((key, value)) => (key, value),
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let oracle: OraclePriceFeed = serde_json::from_slice(&value)?;
            if oracle.key == oracle_id {
                oracle_price_feed.push(oracle);
                if oracle_price_feed.len() as i32 >= limit {
                    break;
                }
            }
        }

        // Sort blocks based on the specified sort order
        match sort_order {
            SortOrder::Ascending => {
                oracle_price_feed.sort_by(|a: &OraclePriceFeed, b| a.id.cmp(&b.id))
            }
            SortOrder::Descending => oracle_price_feed.sort_by(|a, b| b.id.cmp(&a.id)),
        }

        Ok(oracle_price_feed)
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
