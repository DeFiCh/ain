use bitcoin::Txid;
use serde::{Deserialize, Serialize};

pub type TransactionVoutKey = (Txid, usize);

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVout {
    // pub id: String,
    pub vout: usize,
    pub txid: Txid,
    pub n: usize,
    pub value: f64,
    pub token_id: Option<u32>,
    pub script: TransactionVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVoutScript {
    pub hex: Vec<u8>,
    pub r#type: String,
}
