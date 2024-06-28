use super::BlockContext;
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

pub type ScriptUnspentId = (String, String, Txid, String); // hid + hex::encode(block.height) + txid + hex::encode(vout_index)
pub type ScriptUnspentKey = (u32, Txid, usize); // block.height + txid + vout_index

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptUnspent {
    pub id: String,
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
    pub value: f64,
    pub token_id: Option<u32>,
}
