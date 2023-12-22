use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::transaction_vin::TransactionVin,
};

#[derive(Debug)]
pub struct TransactionVinDb {
    pub db: RocksDB,
}

impl TransactionVinDb {
    pub async fn store(&self, trx_vin: TransactionVin) -> Result<()> {
        match serde_json::to_string(&trx_vin) {
            Ok(value) => {
                let key = trx_vin.id.clone();
                self.db
                    .put("transaction_vin", key.as_bytes(), value.as_bytes())?;
                self.db.put(
                    "transaction_vin_mapper",
                    trx_vin.txid.as_bytes(),
                    trx_vin.id.as_bytes(),
                )?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("transaction_vin", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn query(
        &self,
        tx_id: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<TransactionVin>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let transaction_vin: Result<Vec<_>> = self
            .db
            .iterator_by_id("transaction_vin", &tx_id, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let trx_vin: TransactionVin = serde_json::from_slice(&value)?;
                        Ok(trx_vin)
                    })
            })
            .collect();
        Ok(transaction_vin?)
    }
}
