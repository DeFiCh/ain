use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::block::Block,
};

#[derive(Debug)]
pub struct BlockDb {
    pub db: RocksDB,
}

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
    pub async fn store_block(&self, block: Block) -> Result<()> {
        match serde_json::to_string(&block) {
            Ok(value) => {
                let key = block.id.clone();
                self.db.put("raw_block", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete_block(&self, hash: String) -> Result<()> {
        match self.db.delete("raw_block", hash.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
