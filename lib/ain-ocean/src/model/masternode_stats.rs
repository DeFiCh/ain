use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeStats {
    pub id: String,
    pub block: MasternodeStatsBlock,
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
pub struct MasternodeStatsBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeStatsStats {
    pub count: i32,
    pub tvl: String,
    pub locked: Vec<TimelockStats>,
}
