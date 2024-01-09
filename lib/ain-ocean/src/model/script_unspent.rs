use serde::{Deserialize, Serialize};

use super::BlockContext;
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
    pub hex: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptUnspentVout {
    pub txid: String,
    pub n: i32,
    pub value: String,
    pub token_id: i32,
}
