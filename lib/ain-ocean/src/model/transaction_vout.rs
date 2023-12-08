use serde::{Deserialize, Serialize};
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVout {
    pub id: String,
    pub txid: String,
    pub n: i32,
    pub value: String,
    pub token_id: i32,
    pub script: TransactionVoutScript,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TransactionVoutScript {
    pub hex: String,
    pub r#type: String,
}
