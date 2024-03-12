use std::sync::Arc;

use ain_evm::{bytes::Bytes, evm::EVMServices};
use ethereum_types::H256;
use jsonrpsee::{core::RpcResult, proc_macros::rpc};
use rustc_version_runtime;
use sha3::Digest;

#[rpc(server, client, namespace = "web3")]
pub trait MetachainWeb3RPC {
    /// Returns the current network ID as a string.
    #[method(name = "clientVersion")]
    fn client_version(&self) -> RpcResult<String>;
    /// Returns the current network ID as a string.
    #[method(name = "sha3")]
    fn sha3(&self, input: Bytes) -> RpcResult<H256>;
}

pub struct MetachainWeb3RPCModule {
    _handler: Arc<EVMServices>,
}

impl MetachainWeb3RPCModule {
    #[must_use]
    pub fn new(handler: Arc<EVMServices>) -> Self {
        Self { _handler: handler }
    }
}

impl MetachainWeb3RPCServer for MetachainWeb3RPCModule {
    fn client_version(&self) -> RpcResult<String> {
        let version: String = ain_cpp_imports::get_client_version();
        let os = std::env::consts::OS;
        let arch = std::env::consts::ARCH;
        let rust_version = rustc_version_runtime::version();
        Ok(format!(
            "DeFiChain/{}/{}-{}/rustc-{}",
            version, os, arch, rust_version
        ))
    }

    fn sha3(&self, input: Bytes) -> RpcResult<H256> {
        let keccak_256: [u8; 32] = sha3::Keccak256::digest(input.into_vec()).into();
        Ok(H256::from(keccak_256))
    }
}
