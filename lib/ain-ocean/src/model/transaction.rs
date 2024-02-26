use bitcoin::{BlockHash, Txid};
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type TransactionByBlockHashKey = (BlockHash, usize);

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct Transaction {
    pub id: Txid,
    pub order: usize, // tx order
    pub block: BlockContext,
    pub hash: String,
    pub version: u32,
    pub size: u64,
    pub v_size: u64,
    pub weight: u64,
    #[serde(with = "rust_decimal::serde::str")]
    pub total_vout_value: Decimal,
    pub lock_time: u64,
    pub vin_count: usize,
    pub vout_count: usize,
}
