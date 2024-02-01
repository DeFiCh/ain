use bitcoin::Txid;
use serde::{Deserialize, Serialize};

pub type TransactionVoutKey = (Txid, usize);

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVout {
    pub txid: Txid,
    pub n: usize,
    pub value: f64,
    pub token_id: u8,
    pub script: TransactionVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVoutScript {
    pub hex: String,
    pub r#type: String,
}
