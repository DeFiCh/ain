use bitcoin::Txid;
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVout {
    pub id: String, // format!({}{:08x}, txid, usize")
    pub txid: Txid,
    pub n: usize,
    pub value: f64,
    pub token_id: u8,
    pub script: TransactionVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVoutScript {
    pub hex: Vec<u8>,
    pub r#type: String,
}
