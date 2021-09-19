use crate::error::*;
use jsonrpc;
use std::fmt;

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
    /// Creates a client to a DeFiChain JSON-RPC server.
    pub fn new(url: &str, auth: Auth) -> Result<Self, Error> {
        let (user, pass) = auth.get_user_pass();
        jsonrpc::client::Client::simple_http(url, user, pass)
            .map(|client| Client { client })
            .map_err(|e| Error::JsonRpc(e.into()))
    }
}

impl Client {
    /// Call an `cmd` rpc with given `args` list
    pub fn call<T: for<'a> serde::de::Deserialize<'a>>(
        &self,
        cmd: &str,
        args: &[serde_json::Value],
    ) -> Result<T, Error> {
        let raw_args: Vec<_> = args
            .iter()
            .map(|a| serde_json::value::to_raw_value(a))
            .map(|a| a.map_err(|e| Error::Json(e)))
            .collect::<Result<Vec<_>, Error>>()?;

        let req = self.client.build_request(&cmd, &raw_args);
        let resp = self.client.send_request(req).map_err(Error::from);
        Ok(resp?.result()?)
    }

    /// Returns the numbers of block in the longest chain.
    pub fn get_block_count(&self) -> Result<u64, Error> {
        self.call("getblockcount", &[])
    }

    /// Returns the numbers of block at specific height.
    pub fn get_block_hash(&self, height: u64) -> Result<String, Error> {
        self.call("getblockhash", &[height.into()])
    }

    /// Returns the numbers of block in the longest chain.
    pub fn get_best_block_hash(&self) -> Result<String, Error> {
        self.call("getbestblockhash", &[])
    }
}
