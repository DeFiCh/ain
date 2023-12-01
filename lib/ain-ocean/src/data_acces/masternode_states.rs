use crate::database::db_manger::RocksDB;
use crate::model::masternode_stats::MasternodeStats;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasterStatsDb {}
impl MasterStatsDb {
    pub async fn get_latest(&self) -> Result<MasternodeStats> {
        todo!()
    }
    pub async fn query(&self, limit: i32, lt: i32) -> Result<Vec<MasternodeStats>> {
        todo!()
    }
    pub async fn get(&self) -> Result<MasternodeStats> {
        todo!()
    }
    pub async fn store(&self, stats: MasternodeStats) -> Result<()> {
        todo!()
    }
    pub async fn delete(&self, height: i32) -> Result<()> {
        todo!()
    }
}
