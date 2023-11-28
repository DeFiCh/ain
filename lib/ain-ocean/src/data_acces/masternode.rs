use crate::database::RocksDB;
use crate::model::masternode::Masternode;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

pub struct MasterNode {}

impl MasterNode {
    pub async fn query(&self, limit: i32, lt: i32) -> Result(Block) {}
    pub async fn get(&self) -> Result(MasterNode) {}
    pub async fn store(&self, stats: MasterNode) -> Result() {}
    pub async fn delete(&self, id: String) -> Result() {}
}
