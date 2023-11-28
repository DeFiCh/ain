use crate::database::RocksDB;
use crate::model::masternode_stats::MasternodeStats;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasterStatsDb {}
impl MasterStatsDb {
    pub async fn get_latest(&self) -> Result(MasternodeStats) {}
    pub async fn query(&self, limit: i32, lt: i32) -> Result(MasternodeStats) {}
    pub async fn get(&self) -> Result(MasternodeStats) {}
    pub async fn store(&self, stats: MasternodeStats) -> Result(Block) {}
    pub async fn delete(&self, height: i32) -> Result(Block) {}
}
