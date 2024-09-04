use ain_dftx::{Currency, Token, Weightage};
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OracleTokenCurrencyId = (String, String, Txid); //token-currency-oracleId
pub type OracleTokenCurrencyKey = (String, String, u32); //token-currency-height

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OracleTokenCurrency {
    pub id: OracleTokenCurrencyId,
    pub key: OracleTokenCurrencyKey,
    pub token: Token,
    pub currency: Currency,
    pub oracle_id: Txid,
    pub weightage: Weightage,
    pub block: BlockContext,
}
