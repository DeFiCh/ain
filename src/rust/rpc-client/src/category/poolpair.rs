use crate::Client;
use anyhow::Result;
use serde_json::json;

extern crate serde;
use serde::{Deserialize, Serialize};

use std::collections::HashMap;
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

impl Client {
    pub fn get_pool_pair(&self, symbol: (&str, &str)) -> Result<PoolPairInfo> {
        self.call::<PoolPairInfo>(
            "getpoolpair",
            &[format!("{}-{}", symbol.0, symbol.1).into()],
        )
    }

    pub fn create_pool_pair(&self, symbol: (&str, &str)) -> Result<PoolPairInfo> {
        println!("Creating pool pair {}-{}...", symbol.0, symbol.1);
        if let Ok(poolpair) = self.get_pool_pair(symbol) {
            println!("Pool pair already exists.");
            return Ok(poolpair);
        }
        if let Ok(poolpair) = self.get_pool_pair((symbol.1, symbol.0)) {
            println!("Pool pair {}-{} already exists.", symbol.1, symbol.0);
            return Ok(poolpair);
        }

        let token_a = self.get_token(symbol.0)?;
        let token_b = self.get_token(symbol.1)?;

        let owner_address = self.call::<String>("getnewaddress", &[])?;
        let tx = self.call::<String>(
            "createpoolpair",
            &[json!({
                "tokenA": token_a.symbol,
                "tokenB": token_b.symbol,
                "commission": 0.002,
                "status": true,
                "ownerAddress": owner_address,
                "pairSymbol": format!("{}-{}", token_a.symbol, token_b.symbol)
            })],
        )?;
        self.await_n_confirmations(&tx, 1)?;
        self.get_pool_pair(symbol)
    }

    pub fn add_pool_liquidity(
        &self,
        address: &str,
        symbol: (&str, &str),
        amount: (f64, f64),
    ) -> Result<String> {
        self.call::<String>(
            "addpoolliquidity",
            &[
                json!({
                    "*": [format!("{:.8}@{}", amount.0, symbol.0), format!("{:.8}@{}", amount.1, symbol.1)]
                }),
                address.into(),
            ],
        )
    }
}
