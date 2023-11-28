use crate::database::RocksDB;
use crate::model::block::Block;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct BlockDb {}

impl BlockDb {
    pub async fn get_by_hash(&self) -> Result(Block) {}
    pub async fn get_by_height(&self) -> Result(Block) {}
    pub async fn get_heighest(&self) -> Result(Block) {}
    pub async fn query_by_height(&self, limit: i32, lt: i32) -> Result(Block) {}
    pub async fn store_block(&self, block: Block) -> Result(Block) {}
    pub async fn delete_block(&self, hash: String) -> Result(Block) {}
}
