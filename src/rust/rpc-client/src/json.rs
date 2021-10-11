extern crate serde;
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct LoanScheme {
    pub id: String,
    pub mincolratio: i64,
    pub interestrate: f64,
    pub default: bool,
}

#[derive(Clone, PartialEq, Eq, Debug, Deserialize, Serialize)]
pub struct TransactionResult {
    pub confirmations: u32,
}

use std::collections::HashMap;
pub type TokenList = HashMap<String, TokenResult>;

#[derive(Debug, Serialize, Deserialize)]
pub struct TokenResult {
    pub symbol: String,
    #[serde(rename = "symbolKey")]
    pub symbol_key: String,
    pub name: String,
    pub decimal: i64,
    pub limit: i64,
    pub mintable: bool,
    pub tradeable: bool,
    #[serde(rename = "isDAT")]
    pub is_dat: bool,
    #[serde(rename = "isLPS")]
    pub is_lps: bool,
    pub finalized: bool,
    #[serde(rename = "isLoanToken")]
    pub is_loan_token: bool,
    pub minted: f64,
    #[serde(rename = "creationTx")]
    pub creation_tx: String,
    #[serde(rename = "creationHeight")]
    pub creation_height: i64,
    #[serde(rename = "destructionTx")]
    pub destruction_tx: String,
    #[serde(rename = "destructionHeight")]
    pub destruction_height: i64,
    #[serde(rename = "collateralAddress")]
    pub collateral_address: String,
}
