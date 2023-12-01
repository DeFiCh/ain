use crate::database::db_manger::RocksDB;
use crate::model::masternode::Masternode;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct MasterNode {}

impl MasterNode {
    pub async fn query(&self, limit: i32, lt: i32) -> Result<Vec<MasterNode>> {
        todo!()
    }
    pub async fn get(&self) -> Result<MasterNode> {
        todo!()
    }
    pub async fn store(&self, stats: MasterNode) -> Result<()> {
        todo!()
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        todo!()
    }
}
