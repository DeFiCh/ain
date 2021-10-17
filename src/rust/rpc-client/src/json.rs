extern crate serde;
use serde::{Deserialize, Serialize};

#[derive(Clone, PartialEq, Eq, Debug, Deserialize, Serialize)]
pub struct TransactionResult {
    pub confirmations: u32,
}

use std::collections::HashMap;
pub type TokenList = HashMap<String, TokenInfo>;

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]

pub struct TokenInfo {
    pub symbol: String,
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
    pub is_loan_token: bool,
    pub minted: f64,
    pub creation_tx: String,
    pub creation_height: i64,
    pub destruction_tx: String,
    pub destruction_height: i64,
    pub collateral_address: String,
}

pub type PoolPairInfo = HashMap<String, PoolPair>;

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PoolPair {
    pub symbol: String,
    pub name: String,
    pub status: bool,
    pub id_token_a: String,
    pub id_token_b: String,
    pub reserve_a: f64,
    pub reserve_b: f64,
    pub commission: f64,
    pub total_liquidity: f64,
    pub trade_enabled: bool,
    pub owner_address: String,
    pub block_commission_a: f64,
    pub block_commission_b: f64,
    pub reward_pct: f64,
    pub creation_tx: String,
    pub creation_height: i64,
}
