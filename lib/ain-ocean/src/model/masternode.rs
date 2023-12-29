use bitcoin::{BlockHash, ScriptBuf, Txid};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct Masternode {
    pub id: Txid,     // Keep for backward compatibility
    pub sort: String, // Keep for backward compatibility
    pub owner_address: ScriptBuf,
    pub operator_address: ScriptBuf,
    pub creation_height: u32,
    pub resign_height: Option<u32>,
    pub resign_tx: Option<Txid>,
    pub minted_blocks: i32,
    pub timelock: u16,
    pub collateral: String,
    pub block: MasternodeBlock,
    pub history: Vec<HistoryItem>,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct HistoryItem {
    pub owner_address: ScriptBuf,
    pub operator_address: ScriptBuf,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeBlock {
    pub hash: BlockHash,
    pub height: u32,
    pub time: u64,
    pub median_time: u64,
}
