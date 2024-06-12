use std::str::FromStr;

use bitcoin::{address::NetworkUnchecked, Address, Amount, BlockHash, Txid};
use defichain_rpc::json::GetTransactionResultDetailCategory;
use rust_decimal::{prelude::ToPrimitive, Decimal};
use serde::{Deserialize, Serialize};
#[derive(Serialize, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct RawTxDto {
    pub hex: String,
    pub max_fee_rate: Option<Decimal>,
}

pub fn default_max_fee_rate() -> Option<Amount> {
    let default_max_fee_rate = Amount::from_btc(0.1).unwrap_or_default();
    Some(default_max_fee_rate)
}

#[derive(Serialize, Deserialize, Debug)]
pub struct TransctionDetails {
    pub address: Option<Address<NetworkUnchecked>>,
    pub category: GetTransactionResultDetailCategory,
    pub amount: i64,
    pub label: Option<String>,
    pub vout: u32,
    pub fee: Option<i64>,
    pub abandoned: Option<bool>,
    pub hex: String,
    pub blockhash: Option<BlockHash>,
    pub confirmations: i32,
    pub time: u64,
    pub blocktime: Option<u64>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct WalletTxInfo {
    pub confirmations: i32,
    pub blockhash: Option<BlockHash>,
    pub blockindex: Option<usize>,
    pub blocktime: Option<u64>,
    pub blockheight: Option<u32>,
    pub txid: Txid,
    pub time: u64,
    pub timereceived: u64,
    pub bip125_replaceable: Option<String>,
    pub wallet_conflicts: Vec<bitcoin::Txid>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct RawTransaction {
    pub info: WalletTxInfo,
    pub amount: i64,
    pub fee: Option<i64>,
    pub details: Vec<TransctionDetails>,
    pub hex: Vec<u8>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct MempoolAcceptResult {
    pub txid: Txid,
    pub allowed: bool,
    pub reject_reason: Option<String>,
    pub vsize: Option<u64>,
    pub fees: Option<Amount>,
}
