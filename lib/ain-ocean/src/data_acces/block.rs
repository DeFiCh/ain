use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::block::Block;
use anyhow::{anyhow, Error, Result};
use rocksdb::{DBIteratorWithThreadMode, IteratorMode};
use serde::{Deserialize, Serialize};
use std::convert::TryInto;
use std::hash;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::block::Block,
};

#[derive(Debug)]
pub struct BlockDb {
    pub db: RocksDB,
}

impl BlockDb {
    pub async fn get_by_hash(&self, hash: String) -> Result<Option<Block>> {
        let number = match self.db.get("block_map", hash.as_bytes()) {
            Ok(Some(value)) => {
                // Convert the stored bytes to a block number
                let block_number_bytes: [u8; 4] = match value.try_into() {
                    Ok(bytes) => bytes,
                    Err(e) => {
                        return Err(anyhow!("Error converting bytes to block number: {:?}", e))
                    }
                };
                let block_number = i32::from_be_bytes(block_number_bytes);
                Some(block_number)
            }
            Ok(None) => None,
            Err(e) => return Err(anyhow!("Error retrieving block number: {:?}", e)),
        };

        if let Some(block_number) = number {
            let block_key = block_number.to_be_bytes();
            match self.db.get("block", &block_key) {
                Ok(Some(value)) => {
                    let block: Block = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                    Ok(Some(block))
                }
                Ok(None) => Ok(None),
                Err(e) => Err(anyhow!(e)),
            }
        } else {
            Ok(None)
        }
    }

    pub async fn get_by_height(&self, height: i32) -> Result<Option<Block>> {
        match self.db.get("block", &height.to_be_bytes()) {
            Ok(Some(value)) => {
                let block: Block = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(block))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn get_heighest(&self) -> Result<Block> {
        todo!()
    }

    pub async fn query_by_height(&self, limit: i32, lt: i32) -> Result<Vec<Block>> {
        let mut blocks: Vec<Block> = Vec::new();

        // Use the iterator method to create an iterator for the "blocks" column family
        let iterator = self.db.iterator("block", IteratorMode::End)?;

        // Collect the iterator into a vector
        let collected_blocks: Vec<_> = iterator.collect();

        // Iterate over the collected vector in reverse
        for result in collected_blocks.into_iter().rev() {
            let (key, value) = match result {
                Ok((key, value)) => (key, value),
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            // Deserialize the block
            let block: Block = serde_json::from_slice(&value)?;

            // Check height conditions
            if block.height < lt {
                // Collect blocks that meet the conditions
                blocks.push(block.clone());

                // Check if the limit is reached
                if blocks.len() == limit as usize {
                    break;
                }
            }
        }

        Ok(blocks)
    }

    pub async fn put_block(&self, block: Block) -> Result<()> {
        match serde_json::to_string(&block) {
            Ok(value) => {
                let block_number = block.height;
                self.db
                    .put("block", &block_number.to_be_bytes(), value.as_bytes())?;
                let block_map_key = block.hash.as_bytes();
                self.db
                    .put("block_map", block_map_key, &block_number.to_be_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete_block(&self, hash: String) -> Result<()> {
        let number = match self.db.get("block_map", hash.as_bytes()) {
            Ok(Some(value)) => {
                // Convert the stored bytes to a block number
                let block_number_bytes: [u8; 4] = match value.try_into() {
                    Ok(bytes) => bytes,
                    Err(e) => {
                        return Err(anyhow!("Error converting bytes to block number: {:?}", e))
                    }
                };
                let block_number = i32::from_be_bytes(block_number_bytes);
                Some(block_number)
            }
            Ok(None) => None,
            Err(e) => return Err(anyhow!("Error retrieving block number: {:?}", e)),
        };

        if let Some(block_number) = number {
            let block_key = block_number.to_be_bytes();
            match self.db.delete("block", &block_key) {
                Ok(_) => Ok(()),
                Err(e) => Err(anyhow!(e)),
            }
        } else {
            Ok(())
        }
    }
}
