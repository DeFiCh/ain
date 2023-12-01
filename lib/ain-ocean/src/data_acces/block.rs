use crate::database::db_manger::RocksDB;
use crate::model::block::Block;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct BlockDb {}

impl BlockDb {
    pub async fn get_by_hash(&self) -> Result<Block> {
        todo!()
    }
    pub async fn get_by_height(&self) -> Result<Block> {
        todo!()
    }
    pub async fn get_heighest(&self) -> Result<Block> {
        todo!()
    }
    pub async fn query_by_height(&self, limit: i32, lt: i32) -> Result<Vec<Block>> {
        todo!()
    }
    pub async fn store_block(&self, block: Block) -> Result<Block> {
        todo!()
    }
    pub async fn delete_block(&self, hash: String) -> Result<Block> {
        todo!()
    }
}
