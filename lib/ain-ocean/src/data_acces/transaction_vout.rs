use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::transaction_vout::TransactionVout;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Debug)]
pub struct TransactionVoutDb {
    pub db: RocksDB,
}

impl TransactionVoutDb {
    pub async fn get(&self, txid: String, n: i64) -> Result<Option<TransactionVout>> {
        match self.db.get("transaction_vout", txid.as_bytes()) {
            Ok(Some(value)) => {
                let master_node: TransactionVout =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(master_node))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn store(&self, trx_out: TransactionVout) -> Result<()> {
        match serde_json::to_string(&trx_out) {
            Ok(value) => {
                let key = trx_out.id.clone();
                self.db
                    .put("transaction_vout", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("transaction_vout", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn query(&self, txid: String, limit: i32, lt: i32) -> Result<TransactionVout> {
        todo!()
    }
}
