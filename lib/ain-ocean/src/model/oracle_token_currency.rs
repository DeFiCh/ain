use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OracleTokenCurrency {
    pub id: String,
    pub key: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub weightage: i32,
    pub block: BlockContext,
}
