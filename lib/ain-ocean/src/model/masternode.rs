use bitcoin::{BlockHash, Txid};

#[derive(Debug)]
pub struct Masternode {
    pub id: String,
    pub sort: Option<String>,
    pub owner_address: String,
    pub operator_address: String,
    pub creation_height: u32,
    pub resign_height: i32,
    pub resign_tx: Option<String>,
    pub minted_blocks: i32,
    pub timelock: u16,
    pub collateral: String,
    pub block: MasternodeBlock,
    pub history: Option<Vec<HistoryItem>>,
}

#[derive(Debug)]
pub struct MasternodeBlock {
    pub hash: String,
    pub height: u32,
    pub time: u64,
    pub median_time: u64,
}

#[derive(Debug)]
pub struct HistoryItem {
    pub txid: String,
    pub owner_address: String,
    pub operator_address: String,
}
