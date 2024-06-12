use bitcoin::{ScriptBuf, Txid};
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type PoolSwapKey = (u32, u32, usize); // (pool_id, height, txno)

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwap {
    pub id: String,
    pub txid: Txid,
    pub txno: usize,
    pub pool_id: u32,
    pub sort: String,
    pub from_amount: i64,
    pub from_token_id: u64,
    pub to_amount: i64,
    pub to_token_id: u64,
    pub from: ScriptBuf,
    pub to: ScriptBuf,
    pub block: BlockContext,
}
