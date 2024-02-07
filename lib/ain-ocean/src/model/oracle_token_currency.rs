use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type OracleTokenCurrencyId = (String, String, Txid); //token-currency-oracleId
pub type OracleTokenCurrencyKey = (String, String); //token-currency

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OracleTokenCurrency {
    pub id: OracleTokenCurrencyId,
    pub key: OracleTokenCurrencyKey,
    pub token: String,
    pub currency: String,
    pub oracle_id: Txid,
    pub weightage: u8,
    pub block: BlockContext,
}
