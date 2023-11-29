use crate::database::RocksDB;
use crate::model::transaction_vin::TransactionVin;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TransactionVinDb {}

impl TransactionVinDb {
    pub async fn get(&self) -> Result(TransactionVin) {}
    pub async fn store(&self, stats: TransactionVin) -> Result(TransactionVin) {}
    pub async fn delete(&self, id: String) -> Result(TransactionVin) {}
    pub async fn query(&self, txid: String, limit: i32, lt: i32) -> Result(TransactionVin) {}
}
