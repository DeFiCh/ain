use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeStats {
    pub id: String,
    pub block: BlockContext,
    pub stats: MasternodeStatsStats,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TimelockStats {
    pub weeks: i32,
    pub tvl: String,
    pub count: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeStatsStats {
    pub count: i32,
    pub tvl: String,
    pub locked: Vec<TimelockStats>,
}
