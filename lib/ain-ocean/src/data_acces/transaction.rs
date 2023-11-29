use crate::database::RocksDB;
use crate::model::transaction_vin::Transaction;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TransactionVinDb {}

impl TransactionVinDb {
    pub async fn get(&self, txid: String) -> Result(TransactionVin) {}
    pub async fn store(&self, txn: Transaction) -> Result(TransactionVin) {}
    pub async fn delete(&self, txid: String) -> Result(TransactionVin) {}
    pub async fn query_by_blockhash(
        &self,
        hash: String,
        limit: i32,
        lt: i32,
    ) -> Result(TransactionVin) {
    }
}
