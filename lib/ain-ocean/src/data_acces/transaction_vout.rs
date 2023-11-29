use crate::database::RocksDB;
use crate::model::transaction_vout::TransactionVout;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TransactionVinDb {}

impl TransactionVinDb {
    pub async fn get(&self) -> Result(TransactionVout) {}
    pub async fn store(&self, stats: TransactionVout) -> Result(TransactionVout) {}
    pub async fn delete(&self, id: String) -> Result(TransactionVout) {}
    pub async fn query(&self, txid: String, limit: i32, lt: i32) -> Result(TransactionVout) {}
}
