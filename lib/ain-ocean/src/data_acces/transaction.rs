use crate::database::db_manger::RocksDB;
use crate::model::transaction::Transaction;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TransactionVinDb {}

impl TransactionVinDb {
    pub async fn get(&self, txid: String) -> Result<Transaction> {
        todo!()
    }
    pub async fn store(&self, txn: Transaction) -> Result<Transaction> {
        todo!()
    }
    pub async fn delete(&self, txid: String) -> Result<Transaction> {
        todo!()
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
