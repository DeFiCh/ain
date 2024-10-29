use bitcoin::{ScriptBuf, Txid};
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type AuctionHistoryKey = (Txid, [u8; 4], [u8; 4], Txid); // (vault_id, auction_batch_index, block_height, txid)

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct VaultAuctionBatchHistory {
    pub from: ScriptBuf,
    pub amount: i64,
    pub token_id: u64,
    pub block: BlockContext,
}
