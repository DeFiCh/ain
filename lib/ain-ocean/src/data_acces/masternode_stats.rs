use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::masternode_stats::MasternodeStats,
};
use anyhow::Context;
use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;
#[derive(Debug)]
pub struct MasterStatsDb {
    pub db: RocksDB,
}
impl MasterStatsDb {
    pub async fn get_latest(&self) -> Result<Option<MasternodeStats>> {
        let latest_height_bytes = match self
            .db
            .get("masternode_block_height", b"master_block_height")
        {
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
                let block: MasternodeStats =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(block))
            }
            Ok(None) => Ok(None), // No block found for the latest height
            Err(e) => Err(anyhow!(e)),
        }
    }

    pub async fn query(
        &self,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<MasternodeStats>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let master_node: Result<Vec<_>> = self
            .db
            .iterator("masternode_stats", iter_mode)?
            .into_iter()
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let stats: MasternodeStats = serde_json::from_slice(&value)?;
                        if stats.block.height < start_index {
                            Ok(stats)
                        } else {
                            Err(anyhow!("Value is not less than start_index")
                                .context("Contextual error message"))
                        }
                    })
            })
            .collect();
        Ok(master_node?)
    }
    pub async fn get(&self, height: i32) -> Result<Option<MasternodeStats>> {
        let bytes: &[u8] = &height.to_be_bytes();
        match self.db.get("masternode_stats", bytes) {
            Ok(Some(value)) => {
                let master_states: MasternodeStats =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(master_states))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn store(&self, stats: MasternodeStats) -> Result<()> {
        match serde_json::to_string(&stats) {
            Ok(value) => {
                let key = stats.block.height.clone();
                let height: &[u8] = &key.to_be_bytes();
                self.db.put("masternode_stats", height, value.as_bytes())?;
                self.db
                    .put("masternode_map", stats.block.hash.as_bytes(), height)?;
                self.db
                    .delete("masternode_block_height", b"master_block_height")?;
                self.db
                    .put("masternode_block_height", b"master_block_height", height)?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, height: i32) -> Result<()> {
        let bytes: &[u8] = &height.to_be_bytes();
        match self.db.delete("masternode_stats", bytes) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
