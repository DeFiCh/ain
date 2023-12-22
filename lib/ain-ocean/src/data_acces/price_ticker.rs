use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::price_ticker::PriceTicker,
};

pub struct price_ticker {
    pub db: RocksDB,
}

impl price_ticker {
    pub async fn query(
        &self,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<PriceTicker>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let price_ticker: Result<Vec<_>> = self
            .db
            .iterator("price_ticker", iter_mode)?
            .into_iter()
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let pt: PriceTicker = serde_json::from_slice(&value)?;
                        Ok(pt)
                    })
            })
            .collect();
        Ok(price_ticker?)
    }
    pub async fn get(&self, id: String) -> Result<PriceTicker> {
        match self.db.get("price_ticker", id.as_bytes()) {
            Ok(Some(value)) => {
                let pool_swap: PriceTicker =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(pool_swap)
            }
            Ok(None) => Err(anyhow!("No data found for the given ID")),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn put(&self, price: PriceTicker) -> Result<()> {
        match serde_json::to_string(&price) {
            Ok(value) => {
                let key = price.id.clone();
                self.db
                    .put("price_ticker", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("price_ticker", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
