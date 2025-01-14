use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type ScriptUnspentId = ([u8; 32], [u8; 4], Txid, [u8; 8]); // hid + block.height + txid + vout_index
pub type ScriptUnspentKey = ([u8; 4], Txid, [u8; 8]); // block.height + txid + vout_index, ps: key is required in index_script_unspent_vin

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptUnspent {
    pub id: (Txid, [u8; 8]),
    pub hid: [u8; 32],
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
