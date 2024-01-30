use bitcoin::{Amount, ScriptBuf, Sequence, Txid};
use serde::{Deserialize, Serialize};

pub type TransactionVinId = (Txid, Txid, [u8; 4]);
pub type TransactionVinVoutId = (Txid, usize);
#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVin {
    pub id: TransactionVinId,
    pub txid: Txid,
    pub coinbase: String,
    pub vout: TransactionVinVout,
    pub script: TransactionVinScript,
    pub tx_in_witness: Vec<String>,
    pub sequence: Sequence,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVinVout {
    pub id: TransactionVinVoutId,
    pub txid: Txid,
    pub n: i32,
    pub value: u32,
    pub token_id: i32,
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
