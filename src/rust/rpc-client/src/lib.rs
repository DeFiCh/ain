pub mod json;

use jsonrpc;
use std::fmt;

use crate::json::{LoanScheme, TokenList, TokenResult, TransactionResult};
use anyhow::{anyhow, Context, Result};
use serde_json::json;
use std::time::{SystemTime, UNIX_EPOCH};
use std::{thread, time};

/// Client implements a JSON-RPC client for the DeFiChain daemon.
pub struct Client {
    client: jsonrpc::client::Client,
}

impl fmt::Debug for Client {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "defichain_cli::Client({:?})", self.client)
    }
}

pub enum Auth {
    // None,
    UserPass(String, String),
}

impl Auth {
    /// Convert into the arguments that jsonrpc::Client needs.
    fn get_user_pass(self) -> (Option<String>, Option<String>) {
        match self {
            // Auth::None => (None, None),
            Auth::UserPass(u, p) => (Some(u), Some(p)),
        }
    }
}

impl Client {
    pub fn from_env() -> Result<Self> {
        let host = std::env::var("HOST").unwrap_or("127.0.0.1".to_string());
        let port = std::env::var("PORT")
            .unwrap_or("19554".to_string())
            .parse::<u16>()
            .unwrap();
        let user = std::env::var("RPCUSER").unwrap_or("test".to_string());
        let password = std::env::var("RPCPASSWORD").unwrap_or("test".to_string());

        Self::new(
            &format!("http://{}:{}", host, port),
            Auth::UserPass(user.to_string(), password.to_string()),
        )
    }

    /// Creates a client to a DeFiChain JSON-RPC server.
    pub fn new(url: &str, auth: Auth) -> Result<Self> {
        let (user, pass) = auth.get_user_pass();
        jsonrpc::client::Client::simple_http(url, user, pass)
            .map(|client| Client { client })
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
    pub fn await_n_confirmations(&self, tx_hash: &str, n_confirmations: u32) -> Result<u32> {
        for _ in 0..30 {
            let tx_info = self.call::<TransactionResult>("gettransaction", &[tx_hash.into()])?;
            if tx_info.confirmations < n_confirmations {
                thread::sleep(time::Duration::from_secs(2));
            } else {
                return Ok(tx_info.confirmations);
            }
        }
        Err(anyhow!(
            "Transaction {} did not reach {} confirmations.",
            tx_hash,
            n_confirmations
        ))
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

    pub fn create_loan_token(&self, symbol: &str) -> Result<TokenResult> {
        let new_address = self.call::<String>("getnewaddress", &[])?;
        let create_token_tx = self.call::<String>(
            "createtoken",
            &[json!({
                "symbol": symbol,
                "name": "TEST token",
                "isDAT": true,
                "collateralAddress": new_address
            })
            .into()],
        );
        if let Ok(token_tx) = create_token_tx {
            self.await_n_confirmations(&token_tx, 1)?;
        }

        self.call::<TokenList>("listtokens", &[])?
            .into_iter()
            .map(|(_, val)| val)
            .find(|token| token.symbol == symbol)
            .context(format!("Could not find token {}", symbol))
    }

    pub fn get_default_loan_scheme(&self) -> Result<LoanScheme> {
        self.call::<Vec<LoanScheme>>("listloanschemes", &[])?
            .into_iter()
            .find(|scheme| scheme.default == true)
            .context("Could not get default loan scheme")
    }

    pub fn create_oracle(&self, tokens: &[&str], amount: u32) -> Result<String> {
        let oracle_address = self.call::<String>("getnewaddress", &[])?;
        let price_feed = json!(tokens
            .iter()
            .map(|token| json!({ "currency": "USD", "token": token }))
            .collect::<serde_json::Value>());
        let oracle_id = self.call::<String>(
            "appointoracle",
            &[oracle_address.into(), price_feed, 10.into()],
        )?;
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
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generate() -> Result<()> {
        let client = Client::from_env()?;
        let address = client.call::<String>("getnewaddress", &[])?;
        let generated = client.generate(10, &address, 100)?;
        assert_eq!(generated, 10);
        Ok(())
    }

    #[test]
    fn create_loan_tokens() -> Result<()> {
        let client = Client::from_env()?;
        let token = client.create_loan_token("TEST")?;
        assert_eq!(token.symbol, "TEST");
        Ok(())
    }

    #[test]
    fn get_default_loan_scheme() -> Result<()> {
        let client = Client::from_env()?;
        let loan_scheme = client.get_default_loan_scheme()?;
        assert_eq!(loan_scheme.default, true);
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
}
