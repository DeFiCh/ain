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
pub type TokenList = HashMap<String, TokenInfo>;

#[derive(Debug, Serialize, Deserialize)]
pub struct TokenInfo {
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

#[derive(Debug, Serialize, Deserialize)]
pub struct VaultInfo {
    #[serde(rename = "vaultId")]
    pub vault_id: String,
    #[serde(rename = "loanSchemeId")]
    pub loan_scheme_id: String,
    #[serde(rename = "ownerAddress")]
    pub owner_address: String,
    #[serde(rename = "isUnderLiquidation")]
    pub is_under_liquidation: bool,
    #[serde(rename = "collateralAmounts")]
    pub collateral_amounts: Vec<String>,
    #[serde(rename = "loanAmount")]
    pub loan_amount: Vec<String>,
    #[serde(rename = "collateralValue")]
    pub collateral_value: f64,
    #[serde(rename = "loanValue")]
    pub loan_value: f64,
    #[serde(rename = "currentRatio")]
    pub current_ratio: i64,
}

pub type PoolPairInfo = HashMap<String, PoolPair>;
#[derive(Debug, Serialize, Deserialize)]
pub struct PoolPair {
    pub symbol: String,
    pub name: String,
    pub status: bool,
    #[serde(rename = "idTokenA")]
    pub id_token_a: String,
    #[serde(rename = "idTokenB")]
    pub id_token_b: String,
    #[serde(rename = "reserveA")]
    pub reserve_a: f64,
    #[serde(rename = "reserveB")]
    pub reserve_b: f64,
    pub commission: f64,
    #[serde(rename = "totalLiquidity")]
    pub total_liquidity: f64,
    #[serde(rename = "tradeEnabled")]
    pub trade_enabled: bool,
    #[serde(rename = "ownerAddress")]
    pub owner_address: String,
    #[serde(rename = "blockCommissionA")]
    pub block_commission_a: f64,
    #[serde(rename = "blockCommissionB")]
    pub block_commission_b: f64,
    #[serde(rename = "rewardPct")]
    pub reward_pct: f64,
    #[serde(rename = "creationTx")]
    pub creation_tx: String,
    #[serde(rename = "creationHeight")]
    pub creation_height: i64,
}
