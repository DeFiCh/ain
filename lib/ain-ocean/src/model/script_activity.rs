use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone)]
pub enum ScriptActivityType {
    Vin,
    Vout,
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ScriptActivityTypeHex {
    Vin,
    Vout,
}

pub type ScriptActivityId = (u32, ScriptActivityType, Txid, usize); // (block.height, type_hex, txid, index)

#[derive(Debug, Serialize, Deserialize)]

pub struct ScriptActivity {
    pub id: ScriptActivityId,
    pub hid: String,
    pub r#type: ScriptActivityType,
    pub type_hex: ScriptActivityTypeHex,
    pub txid: Txid,
    pub block: BlockContext,
    pub script: ScriptActivityScript,
    pub vin: Option<ScriptActivityVin>,
    pub vout: Option<ScriptActivityVout>,
    pub value: String,
    pub token_id: Option<u32>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptActivityScript {
    pub r#type: String,
    pub hex: Vec<u8>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptActivityVin {
    pub txid: Txid,
    pub n: usize,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptActivityVout {
    pub txid: Txid,
    pub n: usize,
}
