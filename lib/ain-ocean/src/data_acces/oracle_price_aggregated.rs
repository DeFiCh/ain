use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::oracle_price_aggregated::OraclePriceAggregated;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct OraclePriceAggrigatedDb {
    pub db: RocksDB,
}

impl OraclePriceAggrigatedDb {
    pub async fn query(
        &self,
        key: String,
        limit: i32,
        lt: String,
    ) -> Result<Vec<OraclePriceAggregated>> {
        todo!()
    }
    pub async fn put(&self, oracle: OraclePriceAggregated) -> Result<()> {
        match serde_json::to_string(&oracle) {
            Ok(value) => {
                let key = oracle.id.clone();
                self.db
                    .put("oracle_price_aggregated", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn get(&self, id: String) -> Option<OraclePriceAggregated> {
        match self.db.get("oracle_price_aggregated", id.as_bytes()) {
            Ok(Some(value)) => serde_json::from_slice(&value).ok(),
            _ => None,
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("oracle_price_aggregated", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
