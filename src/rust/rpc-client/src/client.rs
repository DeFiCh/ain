use anyhow::Result;
use jsonrpc;
use std::fmt;

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
