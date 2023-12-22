use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::oracle_price_aggregated_interval::OraclePriceAggregatedInterval,
};
use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

pub struct OraclePriceAggregatedIntervalDb {
    pub db: RocksDB,
}

impl OraclePriceAggregatedIntervalDb {
    pub async fn query(
        &self,
        key: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<OraclePriceAggregatedInterval>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let oracle_price_aggregated: Result<Vec<_>> = self
            .db
            .iterator_by_id("oracle_price_aggregated_interval", &key, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let oracle_price_interval: OraclePriceAggregatedInterval =
                            serde_json::from_slice(&value)?;

                        Ok(oracle_price_interval)
                    })
            })
            .collect();
        Ok(oracle_price_aggregated?)
    }
    pub async fn put(&self, oracle: OraclePriceAggregatedInterval) -> Result<()> {
        match serde_json::to_string(&oracle) {
            Ok(value) => {
                let key = oracle.id.clone();
                self.db.put(
                    "oracle_price_aggregated_interval",
                    key.as_bytes(),
                    value.as_bytes(),
                )?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self
            .db
            .delete("oracle_price_aggregated_interval", id.as_bytes())
        {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
