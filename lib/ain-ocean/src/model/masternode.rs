use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct Masternode {
    pub id: String,
    pub sort: String,
    pub owner_address: String,
    pub operator_address: String,
    pub creation_height: i32,
    pub resign_height: i32,
    pub resign_tx: String,
    pub minted_blocks: i32,
    pub timelock: i32,
    pub collateral: String,
    pub block: MasternodeBlock,
    pub history: Vec<HistoryItem>,
}


#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}


#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct HistoryItem {
    pub txid: String,
    pub owner_address: String,
    pub operator_address: String,
}



