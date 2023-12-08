use serde::{Deserialize, Serialize};
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptUnspent {
    pub id: String,
    pub hid: String,
    pub sort: String,
    pub block: ScriptUnspentBlock,
    pub script: ScriptUnspentScript,
    pub vout: ScriptUnspentVout,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptUnspentBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptUnspentScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptUnspentVout {
    pub txid: String,
    pub n: i32,
    pub value: String,
    pub token_id: i32,
}
