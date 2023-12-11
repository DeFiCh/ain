use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OracleTokenCurrency {
    pub id: String,
    pub key: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub weightage: i32,
    pub block: OracleTokenCurrencyBlock,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct OracleTokenCurrencyBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}