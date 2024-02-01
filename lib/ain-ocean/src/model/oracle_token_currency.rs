use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type OracleTokenCurrencyId = (String, String, String); //token-currency-oracleId
pub type OracleTokenCurrencyKey = (String, String); //token-currency

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OracleTokenCurrency {
    pub id: OracleTokenCurrencyId,
    pub key: OracleTokenCurrencyKey,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub weightage: i32,
    pub block: BlockContext,
}
