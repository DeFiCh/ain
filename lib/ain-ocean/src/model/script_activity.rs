use bitcoin::Txid;
use serde::{Deserialize, Serialize};
use std::fmt;

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

impl fmt::Display for ScriptActivityTypeHex {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ScriptActivityTypeHex::Vin => write!(f, "00"),
            ScriptActivityTypeHex::Vout => write!(f, "01"),
        }
    }
}

pub type ScriptActivityId = (String, u32, ScriptActivityTypeHex, Txid, usize); // (hid, block.height, type_hex, txid, index)

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivity {
    pub id: String, // unique id of this output: block height, type, txid(vin/vout), n(vin/vout)
    pub hid: String, // hashed id, for length compatibility reasons this is the hashed id of script
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
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityScript {
    pub r#type: String,
    pub hex: Vec<u8>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityVin {
    pub txid: Txid,
    pub n: usize,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityVout {
    pub txid: Txid,
    pub n: usize,
}
