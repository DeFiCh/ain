use ain_dftx::Weightage;
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OracleTokenCurrencyId = (String, String, Txid); //token-currency-oracleId

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct OracleTokenCurrency {
    pub weightage: Weightage,
    pub block: BlockContext,
}
