use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB, SortOrder},
    model::oracle_price_aggregated::OraclePriceAggregated,
};
use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

pub struct OraclePriceAggrigatedDb {
    pub db: RocksDB,
}

impl OraclePriceAggrigatedDb {
    pub async fn query(
        &self,
        oracle_key: String,
        limit: i32,
        lt: String,
        sort_order: SortOrder,
    ) -> Result<Vec<OraclePriceAggregated>> {
        let iterator = self
            .db
            .iterator("oracle_price_aggregated", IteratorMode::End)?;
        let mut oracle_pa: Vec<OraclePriceAggregated> = Vec::new();
        let collected_blocks: Vec<_> = iterator.collect();

        for result in collected_blocks.into_iter().rev() {
            let (key, value) = match result {
                Ok((key, value)) => (key, value),
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let oracle: OraclePriceAggregated = serde_json::from_slice(&value)?;
            if oracle.key == oracle_key {
                oracle_pa.push(oracle);
                if oracle_pa.len() as i32 >= limit {
                    break;
                }
            }
        }

        // Sort blocks based on the specified sort order
        match sort_order {
            SortOrder::Ascending => {
                oracle_pa.sort_by(|a: &OraclePriceAggregated, b| a.id.cmp(&b.id))
            }
            SortOrder::Descending => oracle_pa.sort_by(|a, b| b.id.cmp(&a.id)),
        }

        Ok(oracle_pa)
    }
    pub async fn put(&self, oracle: OraclePriceAggregated) -> Result<()> {
        match serde_json::to_string(&oracle) {
            Ok(value) => {
                let key = oracle.key.clone();
                self.db
                    .put("oracle_price_aggregated", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn get(&self, key: String) -> Option<OraclePriceAggregated> {
        match self.db.get("oracle_price_aggregated", key.as_bytes()) {
            Ok(Some(value)) => serde_json::from_slice(&value).ok(),
            _ => None,
        }
    }
    pub async fn delete(&self, key: String) -> Result<()> {
        match self.db.delete("oracle_price_aggregated", key.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
