use serde::{Deserialize, Serialize};

use super::BlockContext;
#[derive(Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ScriptActivityType {
    Vin,
    Vout,
}

#[derive(Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ScriptActivityTypeHex {
    Vin,
    Vout,
}

#[derive(Debug, Serialize, Deserialize)]

pub struct ScriptActivity {
    pub id: String,
    pub hid: String,
    pub r#type: ScriptActivityType,
    pub type_hex: ScriptActivityTypeHex,
    pub txid: String,
    pub block: BlockContext,
    pub script: ScriptActivityScript,
    pub vin: ScriptActivityVin,
    pub vout: ScriptActivityVout,
    pub value: String,
    pub token_id: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptActivityScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptActivityVin {
    pub txid: String,
    pub n: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptActivityVout {
    pub txid: String,
    pub n: i32,
}
