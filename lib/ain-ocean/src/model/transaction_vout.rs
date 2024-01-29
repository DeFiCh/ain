use bitcoin::{ScriptBuf, Txid};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVout {
    pub id: String,
    pub txid: Txid,
    pub n: i32,
    pub value: String,
    pub token_id: i32,
    pub script: TransactionVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVoutScript {
    pub hex: ScriptBuf,
    pub r#type: String,
}
