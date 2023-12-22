use std::convert::TryInto;

use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
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

    pub async fn get_highest(&self) -> Result<Option<Block>> {
        // Retrieve the latest block height
        let latest_height_bytes = match self.db.get("latest_block_height", b"latest_block_height") {
            Ok(Some(value)) => value,
            Ok(None) => return Ok(None), // No latest block height set
            Err(e) => return Err(anyhow!(e)),
        };

        // Convert the latest height bytes back to an integer
        let latest_height = i32::from_be_bytes(
            latest_height_bytes
                .as_slice()
                .try_into()
                .map_err(|_| anyhow!("Byte length mismatch for latest height"))?,
        );

        // Retrieve the block with the latest height
        match self.db.get("block", &latest_height.to_be_bytes()) {
            Ok(Some(value)) => {
                let block: Block = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(block))
            }
            Ok(None) => Ok(None), // No block found for the latest height
            Err(e) => Err(anyhow!(e)),
        }
    }

    pub async fn query_by_height(
        &self,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<Block>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let blocks: Result<Vec<_>> = self
            .db
            .iterator("block", iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let bl: Block = serde_json::from_slice(&value)?;
                        Ok(bl)
                    })
            })
            .collect();

        Ok(blocks?)
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
                self.db
                    .delete("latest_block_height", b"latest_block_height")?;
                self.db.put(
                    "latest_block_height",
                    b"latest_block_height",
                    &block_number.to_be_bytes(),
                )?;
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
