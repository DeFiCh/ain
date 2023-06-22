use ain_evm::bytes::Bytes;
use digest::Digest;
use jsonrpsee::core::RpcResult;
use jsonrpsee::proc_macros::rpc;
use primitive_types::H256;

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
        let version = env!("CARGO_PKG_VERSION");
        let commit = option_env!("GIT_HASH").unwrap_or("unknown");
        let os = std::env::consts::OS;

        Ok(format!("Metachain/v{}/{}-{}", version, os, commit))
    }

    fn sha3(&self, input: Bytes) -> RpcResult<H256> {
        let keccak_256: [u8; 32] = sha3::Keccak256::digest(&input.into_vec()).into();
        Ok(H256::from(keccak_256))
    }
}
