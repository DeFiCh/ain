use bitcoin::{BlockHash, Txid};
use serde::{Deserialize, Serialize};
use serde_with::{serde_as, DisplayFromStr};

use super::BlockContext;

pub type TransactionByBlockHashKey = (BlockHash, usize);

#[serde_as]
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct Transaction {
    pub id: Txid, // unique id of the transaction, same as the txid
    pub order: usize, // tx order
    pub block: BlockContext,
    pub hash: String,
    pub version: u32,
    pub size: u64,
    pub v_size: u64,
    pub weight: u64,
    #[serde_as(as = "DisplayFromStr")]
    pub total_vout_value: f64,
    pub lock_time: u64,
    pub vin_count: usize,
    pub vout_count: usize,
}
