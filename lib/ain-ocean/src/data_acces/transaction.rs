use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::transaction::Transaction,
};

#[derive(Debug)]
pub struct TransactionVinDb {
    pub db: RocksDB,
}

impl TransactionVinDb {
    pub async fn get(&self, txid: String) -> Result<Option<Transaction>> {
        match self.db.get("transaction", txid.as_bytes()) {
            Ok(Some(value)) => {
                let trx: Transaction = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(trx))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn store(&self, txn: Transaction) -> Result<()> {
        match serde_json::to_string(&txn) {
            Ok(value) => {
                let key = txn.id.clone();
                self.db
                    .put("transaction", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, txid: String) -> Result<()> {
        match self.db.delete("transaction", txid.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn query_by_blockhash(
        &self,
        hash: String,
        limit: i32,
        lt: i32,
    ) -> Result<Vec<Transaction>> {
        todo!()
    }
}
