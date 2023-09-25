use ain_evm::bytes::Bytes;
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use primitive_types::H256;
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

#[derive(Default)]
pub struct MetachainWeb3RPCModule {}

impl MetachainWeb3RPCServer for MetachainWeb3RPCModule {
    fn client_version(&self) -> RpcResult<String> {
        let version: String = ain_cpp_imports::get_client_version();
        let commit = option_env!("GIT_HASH")
            .ok_or_else(|| Error::Custom(format!("missing GIT_HASH env var")))?;
        let os = std::env::consts::OS;

        Ok(format!("Metachain/v{}/{}-{}", version, os, commit))
    }

    fn sha3(&self, input: Bytes) -> RpcResult<H256> {
        let keccak_256: [u8; 32] = sha3::Keccak256::digest(&input.into_vec()).into();
        Ok(H256::from(keccak_256))
    }
}
