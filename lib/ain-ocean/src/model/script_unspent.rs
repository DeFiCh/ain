use super::BlockContext;
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

pub type ScriptUnspentId = (Txid, usize); // txid + vout_index

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptUnspent {
    pub id: ScriptUnspentId,
    pub hid: String,
    pub sort: String,
    pub block: BlockContext,
    pub script: ScriptUnspentScript,
    pub vout: ScriptUnspentVout,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptUnspentScript {
    pub r#type: String,
    pub hex: Vec<u8>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptUnspentVout {
    pub txid: Txid,
    pub n: usize,
    pub value: String,
    pub token_id: Option<u32>,
}
