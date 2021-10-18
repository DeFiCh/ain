extern crate serde;
use crate::Client;
use anyhow::{Context, Result};
use serde_json::json;

use serde::{Deserialize, Serialize};

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

impl Client {
    pub fn create_token(&self, symbol: &str) -> Result<TokenInfo> {
        println!("Creating token {}...", symbol);

        if let Ok(token) = self.get_token(symbol) {
            println!("Token {} already exists", symbol);
            return Ok(token);
        }

        let collateral_address = self.call::<String>("getnewaddress", &[])?;
        println!(
            "Token {} collateral address : {}",
            symbol, collateral_address
        );
        let tx = self.call::<String>(
            "createtoken",
            &[json!({
                "symbol": symbol,
                "name": format!("{} token", symbol),
                "isDAT": true,
                "collateralAddress": collateral_address
            })
            .into()],
        )?;
        self.await_n_confirmations(&tx, 1)?;
        self.get_token(symbol)
    }

    pub fn get_token(&self, symbol: &str) -> Result<TokenInfo> {
        self.call::<TokenList>("listtokens", &[])?
            .into_iter()
            .map(|(_, val)| val)
            .find(|token| token.symbol == symbol)
            .context(format!("Could not find token {}", symbol))
    }

    pub fn mint_tokens(&self, amount: u32, symbol: &str) -> Result<String> {
        self.call::<String>("minttokens", &[format!("{}@{}", amount, symbol).into()])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn create_token() -> Result<()> {
        let client = Client::from_env()?;
        let token = client.create_token("TEST")?;
        assert_eq!(token.symbol, "TEST");
        Ok(())
    }

    #[test]
    fn get_token() -> Result<()> {
        let client = Client::from_env()?;
        client.get_token("DFI")?;
        Ok(())
    }
}
