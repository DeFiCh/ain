use bitcoin::{Amount, ScriptBuf, Txid};
use serde::{Deserialize, Serialize};

pub type TransactionVoutId = (Txid, usize);
#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVout {
    pub id: TransactionVoutId,
    pub txid: Txid,
    pub n: i32,
    pub value: Amount,
    pub token_id: i32,
    pub script: TransactionVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVoutScript {
    pub hex: ScriptBuf,
    pub r#type: String,
}
