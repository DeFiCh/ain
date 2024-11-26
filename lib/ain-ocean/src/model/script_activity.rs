use std::fmt;

use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
#[derive(Debug, PartialEq, Eq, Serialize, Deserialize, Clone)]
pub enum ScriptActivityType {
    Vin,
    Vout,
}

impl fmt::Display for ScriptActivityType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::Vin => write!(f, "vin"),
            Self::Vout => write!(f, "vout"),
        }
    }
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ScriptActivityTypeHex {
    Vin,
    Vout,
}

impl fmt::Display for ScriptActivityTypeHex {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Self::Vin => write!(f, "00"),
            Self::Vout => write!(f, "01"),
        }
    }
}

pub type ScriptActivityId = ([u8; 32], [u8; 4], ScriptActivityTypeHex, Txid, [u8; 8]); // (hid, block.height, type_hex, txid, index)

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptActivity {
    pub hid: [u8; 32], // hashed id, for length compatibility reasons this is the hashed id of script
    pub r#type: ScriptActivityType,
    pub type_hex: ScriptActivityTypeHex,
    pub txid: Txid,
    pub block: BlockContext,
    pub script: ScriptActivityScript,
    pub vin: Option<ScriptActivityVin>,
    pub vout: Option<ScriptActivityVout>,
    pub value: f64,
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
