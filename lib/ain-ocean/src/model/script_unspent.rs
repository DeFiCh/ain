use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptUnspent {
    pub id: String,
    pub hid: String,
    pub sort: String,
    pub block: ScriptUnspentBlock,
    pub script: ScriptUnspentScript,
    pub vout: ScriptUnspentVout,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptUnspentBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptUnspentScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptUnspentVout {
    pub txid: String,
    pub n: i32,
    pub value: String,
    pub token_id: i32,
}
