use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::oracle_price_aggregated::OraclePriceAggregated,
};

pub struct OraclePriceAggrigatedDb {
    pub db: RocksDB,
}

impl OraclePriceAggrigatedDb {
    pub async fn query(
        &self,
        oracle_key: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<OraclePriceAggregated>> {
        let iter_mode: IteratorMode<'static> =
            MyIteratorMode::from((sort_order, start_index)).into();
        let oracle_price_aggregated: Result<Vec<_>> = self
            .db
            .iterator_by_id("oracle_price_aggregated", &oracle_key, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let oracle_price_aggr: OraclePriceAggregated =
                            serde_json::from_slice(&value)?;

                        Ok(oracle_price_aggr)
                    })
            })
            .collect();
        Ok(oracle_price_aggregated?)
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
