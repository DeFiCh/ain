use bitcoin::{ScriptBuf, Sequence, Txid};
use serde::{Deserialize, Serialize};

pub type TransactionVinKey = (Txid, Txid, u32);

pub type TransactionVinVoutKey = (Txid, usize);
#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVin {
    pub txid: Txid,
    pub coinbase: String,
    pub vout: TransactionVinVout,
    pub script: TransactionVinScript,
    pub tx_in_witness: Vec<String>,
    pub sequence: Sequence,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVinVout {
    pub id: TransactionVinVoutKey,
    pub txid: Txid,
    pub n: i32,
    pub value: u32,
    pub token_id: u8, // Can constrain u8 since it's unused and hardcoded to 0
    pub script: TransactionVinVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVinScript {
    pub hex: ScriptBuf,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVinVoutScript {
    pub hex: ScriptBuf,
}
