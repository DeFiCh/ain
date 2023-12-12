use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::oracle_price_aggregated_interval::OraclePriceAggregatedInterval;
use anyhow::{anyhow, Result};
use rocksdb::{ColumnFamilyDescriptor, IteratorMode, DB};
use serde::{Deserialize, Serialize};
pub struct OraclePriceAggregatedIntervalDb {
    pub db: RocksDB,
}

impl OraclePriceAggregatedIntervalDb {
    pub async fn query(
        &self,
        key: String,
        limit: i32,
        lt: String,
    ) -> Result<Vec<OraclePriceAggregatedInterval>> {
        let iterator = self
            .db
            .iterator("oracle_price_aggregated_interval", IteratorMode::End)?;
        let mut oracle_prices: Vec<OraclePriceAggregatedInterval> = Vec::new();
        let collected_items: Vec<_> = iterator.collect();

        for result in collected_items.into_iter().rev() {
            let (key, value) = match result {
                Ok((key, value)) => (key, value),
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let oracle_price: OraclePriceAggregatedInterval = serde_json::from_slice(&value)?;

            // Check if the id is less than 'lt'
            if String::from_utf8(key.to_vec())? < lt {
                oracle_prices.push(oracle_price);

                if oracle_prices.len() == limit as usize {
                    break;
                }
            }
        }

        Ok(oracle_prices)
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
