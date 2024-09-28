use bitcoin::{Amount, Txid};
use defichain_rpc::json::{GetRawTransactionResultVin, GetRawTransactionResultVout};
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};
#[derive(Serialize, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct RawTxDto {
    pub hex: String,
    pub max_fee_rate: Option<Decimal>,
}

pub fn default_max_fee_rate() -> Amount {
    Amount::from_btc(0.1).unwrap_or_default()
}

#[derive(Clone, PartialEq, Eq, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RawTransactionResult {
    pub in_active_chain: Option<bool>,
    pub hex: Vec<u8>,
    pub txid: bitcoin::Txid,
    pub hash: bitcoin::Wtxid,
    pub size: usize,
    pub vsize: usize,
    pub version: u32,
    pub locktime: u32,
    pub vin: Vec<GetRawTransactionResultVin>,
    pub vout: Vec<GetRawTransactionResultVout>,
    pub blockhash: Option<bitcoin::BlockHash>,
    pub confirmations: Option<u32>,
    pub time: Option<usize>,
    pub blocktime: Option<usize>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct MempoolAcceptResult {
    pub txid: Txid,
    pub allowed: bool,
    pub reject_reason: Option<String>,
    pub vsize: Option<u64>,
    pub fees: Option<Amount>,
}
