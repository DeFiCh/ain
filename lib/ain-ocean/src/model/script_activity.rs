use serde::{Deserialize, Serialize};
#[derive(Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub enum ScriptActivityType {
    #[default]
    Vin,
    Vout,
}

impl ScriptActivityType {
    pub fn as_str(&self) -> &'static str {
        match self {
            ScriptActivityType::Vin => "vin",
            ScriptActivityType::Vout => "vout",
        }
    }
}

#[derive(Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub enum ScriptActivityTypeHex {
    #[default]
    Vin,
    Vout,
}

impl ScriptActivityTypeHex {
    pub fn as_str(&self) -> &'static str {
        match self {
            ScriptActivityTypeHex::Vin => "00",
            ScriptActivityTypeHex::Vout => "01",
        }
    }
}

#[derive(Debug, Default, Serialize, Deserialize)]

pub struct ScriptActivity {
    pub id: String,
    pub hid: String,
    pub r#type: ScriptActivityType,
    pub type_hex: ScriptActivityTypeHex,
    pub txid: String,
    pub block: ScriptActivityBlock,
    pub script: ScriptActivityScript,
    pub vin: ScriptActivityVin,
    pub vout: ScriptActivityVout,
    pub value: String,
    pub token_id: i32,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptActivityBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptActivityScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptActivityVin {
    pub txid: String,
    pub n: i32,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptActivityVout {
    pub txid: String,
    pub n: i32,
}