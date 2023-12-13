use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::database::db_manger::SortOrder;
use crate::model::price_ticker::PriceTicker;
use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

pub struct price_ticker {
    pub db: RocksDB,
}

impl price_ticker {
    pub async fn query(
        &self,
        limit: i32,
        lt: String,
        sort_order: SortOrder,
    ) -> Result<Vec<PriceTicker>> {
        let iterator = self.db.iterator("price_ticker", IteratorMode::End)?;
        let mut pt: Vec<PriceTicker> = Vec::new();
        let collected_items: Vec<_> = iterator.collect();

        for result in collected_items.into_iter().rev() {
            let value = match result {
                Ok((_, value)) => value,
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let price_ticker: PriceTicker = serde_json::from_slice(&value)?;
            pt.push(price_ticker);
            if pt.len() as i32 >= limit {
                break;
            }
        }

        match sort_order {
            SortOrder::Ascending => pt.sort_by(|a, b| a.id.cmp(&b.id)),
            SortOrder::Descending => pt.sort_by(|a, b| b.id.cmp(&a.id)),
        }

        Ok(pt)
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
