use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::oracle_price_aggregated_interval::OraclePriceAggregatedInterval;
use anyhow::{anyhow, Result};
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
        todo!()
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
