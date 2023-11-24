use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
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

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityVin {
    pub txid: String,
    pub n: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityVout {
    pub txid: String,
    pub n: i32,
}
