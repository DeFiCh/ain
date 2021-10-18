mod category;
pub mod json;

use jsonrpc;
use std::fmt;

use anyhow::{anyhow, Context, Result};
use json::{PoolPairInfo, TokenInfo, TokenList, TransactionResult};
use serde_json::json;
use std::time::{SystemTime, UNIX_EPOCH};
use std::{thread, time};

/// Client implements a JSON-RPC client for the DeFiChain daemon.
pub struct Client {
    pub client: jsonrpc::client::Client,
    pub network: String,
}

impl fmt::Debug for Client {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "defichain_cli::Client({:?})", self.client)
    }
}

impl Client {
    pub fn from_env() -> Result<Self> {
        let host = std::env::var("HOST").unwrap_or("127.0.0.1".to_string());
        let port = std::env::var("PORT")
            .unwrap_or("19554".to_string())
            .parse::<u16>()
            .unwrap();
        let user = std::env::var("RPCUSER").unwrap_or("cake".to_string());
        let password = std::env::var("RPCPASSWORD").unwrap_or("cake".to_string());
        let network = std::env::var("NETWORK").unwrap_or("regtest".to_string());

        Self::new(
            &format!("http://{}:{}", host, port),
            user,
            password,
            network,
        )
    }

    /// Creates a client to a DeFiChain JSON-RPC server.
    pub fn new(url: &str, user: String, password: String, network: String) -> Result<Self> {
        jsonrpc::client::Client::simple_http(url, Some(user), Some(password))
            .map(|client| Client { client, network })
            .map_err(|e| e.into())
    }
}

impl Client {
    // / Call an `cmd` rpc with given `args` list
    pub fn call<T: for<'a> serde::de::Deserialize<'a>>(
        &self,
        cmd: &str,
        args: &[serde_json::Value],
    ) -> Result<T> {
        let raw_args: Vec<_> = args
            .iter()
            .map(|a| serde_json::value::to_raw_value(a))
            .map(|a| a.map_err(|e| e.into()))
            .collect::<Result<Vec<_>>>()?;

        let req = self.client.build_request(&cmd, &raw_args);
        let resp = self.client.send_request(req)?;
        Ok(resp.result()?)
    }
}

impl Client {
    pub fn await_n_confirmations(&self, tx_hash: &str, n_confirmations: u32) -> Result<()> {
        for _ in 0..132 {
            // 132 * 5 == 11min. Max duration for a tx to be confirmed
            let tx_info = self.call::<TransactionResult>("gettransaction", &[tx_hash.into()])?;
            if tx_info.confirmations < n_confirmations {
                thread::sleep(time::Duration::from_secs(5));
            } else {
                return Ok(());
            }
        }
        Err(anyhow!(
            "Transaction {} did not reach {} confirmations.",
            tx_hash,
            n_confirmations
        ))
    }

    pub fn wait_for_balance_gte(&self, balance: f32) -> Result<()> {
        loop {
            let get_balance = self.call::<f32>("getbalance", &[])?;
            if get_balance < balance {
                println!(
                    "current balance : {}, waiting to reach {}",
                    get_balance, balance,
                );
                println!(
                    "current blockheight : {}",
                    self.call::<u32>("getblockcount", &[])?
                );
                thread::sleep(time::Duration::from_secs(2));
            } else {
                return Ok(());
            }
        }
    }

    pub fn generate(&self, n: u16, address: &str, max_tries: u16) -> Result<u16> {
        let mut count: u16 = 0;
        for _ in 0..max_tries {
            let generated =
                self.call::<u16>("generatetoaddress", &[1.into(), address.into(), 1.into()])?;
            if generated == 1 {
                count += 1
            }
            if count == n {
                return Ok(count);
            }
        }
        Ok(count)
    }

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

    pub fn create_oracle(&self, tokens: &[&str], amount: f32) -> Result<String> {
        println!("Appointing oracle for tokens {}", tokens.join(", "));
        let oracle_address = self.get_new_address()?;
        let price_feed = json!(tokens
            .iter()
            .map(|token| json!({ "currency": "USD", "token": token }))
            .collect::<serde_json::Value>());
        let oracle_id = self.appoint_oracle(&oracle_address, price_feed, 1)?;
        self.await_n_confirmations(&oracle_id, 1)?;

        let oracle_prices = json!(tokens
            .iter()
            .map(
                |token| json!({ "currency": "USD", "tokenAmount": format!("{}@{}", amount, token) })
            )
            .collect::<serde_json::Value>());
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        let set_oracle_tx = self.call::<String>(
            "setoracledata",
            &[oracle_id.clone().into(), timestamp.into(), oracle_prices],
        )?;
        self.await_n_confirmations(&set_oracle_tx, 1)?;

        Ok(oracle_id)
    }

    pub fn appoint_oracle(&self, oracle_address: &str, price_feed: serde_json::Value, weightage: u32) -> Result<String> {
        self.call::<String>(
            "appointoracle",
            &[oracle_address.into(), price_feed, weightage.into()],
        )
    }

    pub fn set_oracle_data(&self, oracle_id: &str, token: &str, amount: u32) -> Result<String> {
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        self.call::<String>(
            "setoracledata",
            &[
                oracle_id.clone().into(),
                timestamp.into(),
                json!([{ "currency": "USD", "tokenAmount": format!("{}@{}", amount, token) }]),
            ],
        )
    }

    pub fn get_token(&self, symbol: &str) -> Result<TokenInfo> {
        self.call::<TokenList>("listtokens", &[])?
            .into_iter()
            .map(|(_, val)| val)
            .find(|token| token.symbol == symbol)
            .context(format!("Could not find token {}", symbol))
    }

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
        amount: (u32, u32),
    ) -> Result<String> {
        self.call::<String>(
            "addpoolliquidity",
            &[
                json!({
                    "*": [format!("{}@{}", amount.0, symbol.0), format!("{}@{}", amount.1, symbol.1)]
                }),
                address.into(),
            ],
        )
    }

    pub fn get_new_address(&self) -> Result<String> {
        self.call::<String>("getnewaddress", &[])
    }

    pub fn utxo_to_account(&self, account: &str, amount: &str) -> Result<String> {
        self.call::<String>("utxostoaccount", &[json!({ account: amount })])
    }

    pub fn mint_tokens(&self, amount: u32, symbol: &str) -> Result<String> {
        self.call::<String>("minttokens", &[format!("{}@{}", amount, symbol).into()])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generate() -> Result<()> {
        let client = Client::from_env()?;
        if client.network == "regtest" {
            let address = client.call::<String>("getnewaddress", &[])?;
            let generated = client.generate(10, &address, 100)?;
            assert_eq!(generated, 10);
        }
        Ok(())
    }

    #[test]
    fn create_token() -> Result<()> {
        let client = Client::from_env()?;
        let token = client.create_token("TEST")?;
        assert_eq!(token.symbol, "TEST");
        Ok(())
    }

    #[test]
    fn create_oracle() -> Result<()> {
        let client = Client::from_env()?;
        let oracle_id = client.create_oracle(&["DFI", "BTC", "TSLA", "GOOGL"], 10);
        assert!(oracle_id.is_ok());
        client.call::<String>("removeoracle", &[oracle_id.unwrap().into()])?;
        Ok(())
    }

    #[test]
    fn get_token() -> Result<()> {
        let client = Client::from_env()?;
        client.get_token("DFI")?;
        Ok(())
    }
}
