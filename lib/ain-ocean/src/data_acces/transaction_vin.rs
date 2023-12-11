use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
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
                self.db.put("oracle", key.as_bytes(), value.as_bytes())?;
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
    pub async fn query(&self, txid: String, limit: i32, lt: i32) -> Result<Vec<TransactionVin>> {
        todo!()
    }
}
