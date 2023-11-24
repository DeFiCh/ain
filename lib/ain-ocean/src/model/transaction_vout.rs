use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TransactionVout {
    pub id: String,
    pub txid: String,
    pub n: i32,
    pub value: String,
    pub token_id: i32,
    pub script: TransactionVoutScript,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct TransactionVoutScript {
    pub hex: String,
    pub r#type: String,
}
