use crate::database::db_manger::RocksDB;
use crate::model::transaction_vin::TransactionVin;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TransactionVinDb {}

impl TransactionVinDb {
    pub async fn get(&self) -> Result<TransactionVin> {
        todo!()
    }
    pub async fn store(&self, stats: TransactionVin) -> Result<()> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
    pub async fn query(&self, txid: String, limit: i32, lt: i32) -> Result<Vec<TransactionVin>> {
        todo!()
    }
}
