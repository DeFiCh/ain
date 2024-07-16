use bitcoin::{ScriptBuf, Txid};
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type AuctionHistoryKey = (Txid, u32, Txid); // (vault_id, auction_batch_index, txid)
pub type AuctionHistoryByHeightKey = (Txid, u32, u32, usize); // (vault_id, auction_batch_index, block_height, txid)

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct VaultAuctionBatchHistory {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub vault_id: Txid,
    pub index: usize,
    pub from: ScriptBuf,
    pub amount: i64,
    pub token_id: u64,
    pub block: BlockContext,
}
