use bitcoin::{Amount, ScriptBuf, Sequence, Txid};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVin {
    pub id: String,
    pub txid: Txid,
    pub coinbase: String,
    pub vout: TransactionVinVout,
    pub script: TransactionVinScript,
    pub tx_in_witness: Vec<String>,
    pub sequence: Sequence,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVinVout {
    pub id: String,
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
