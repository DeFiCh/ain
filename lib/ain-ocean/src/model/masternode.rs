use bitcoin::Txid;
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Debug, Serialize, Deserialize)]
pub struct Masternode {
    pub id: Txid,     // Keep for backward compatibility
    pub sort: String, // Keep for backward compatibility
    pub owner_address: String,
    pub operator_address: String,
    pub creation_height: u32,
    pub resign_height: Option<u32>,
    pub resign_tx: Option<Txid>,
    pub minted_blocks: i32,
    pub timelock: u16,
    pub collateral: Decimal,
    pub block: BlockContext,
    pub history: Vec<HistoryItem>,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct HistoryItem {
    pub owner_address: String,
    pub operator_address: String,
}
