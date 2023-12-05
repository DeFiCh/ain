use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::RocksDB;
use crate::model::masternode_stats::MasternodeStats;
use anyhow::{anyhow, Result};
use bitcoin::absolute::Height;
use rocksdb::{ColumnFamilyDescriptor, IteratorMode, DB};
use serde::{Deserialize, Serialize};

#[derive(Debug)]
pub struct MasterStatsDb {
    pub db: RocksDB,
}
impl MasterStatsDb {
    pub async fn get_latest(&self) -> Result<MasternodeStats> {
        // let mut latest_stats: Option<MasternodeStats> = None;
        // let mut highest_height = -1;

        // let iter = self.db.iterator("masternode_stats", IteratorMode::End); // Start from the end of the DB

        // for (key, value) in iter {
        //     let stats: MasternodeStats = serde_json::from_slice(&value)?;
        //     if stats.block.height > highest_height {
        //         highest_height = stats.block.height;
        //         latest_stats = Some(stats);
        //     }
        // }

        // Ok(latest_stats);
        todo!()
    }
    pub async fn query(&self, limit: i32, lt: i32) -> Result<Vec<MasternodeStats>> {
        todo!()
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
                let bytes: &[u8] = &key.to_be_bytes();
                self.db.put("masternode_stats", bytes, value.as_bytes())?;
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
