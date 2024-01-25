use bitcoin::{BlockHash, Txid};
use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct Transaction {
    pub id: Txid,
    pub order: usize,
    pub block: BlockContext,
    pub hash: BlockHash,
    pub version: i32,
    pub size: usize,
    pub v_size: usize,
    pub weight: u64,
    pub total_vout_value: u64,
    pub lock_time: u32,
    pub vin_count: usize,
    pub vout_count: usize,
}
