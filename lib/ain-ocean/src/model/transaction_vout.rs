use bitcoin::{Amount, ScriptBuf, Txid};
use serde::{Deserialize, Serialize};

pub type TransactionVoutKey = (Txid, usize);

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVout {
    pub txid: Txid,
    pub n: usize,
    pub value: Amount,
    pub token_id: u8, // Can constrain u8 since it's unused and hardcoded to 0
    pub script: TransactionVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVoutScript {
    pub hex: ScriptBuf,
    pub r#type: String,
}
