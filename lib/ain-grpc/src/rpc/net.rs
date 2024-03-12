use std::sync::Arc;

use ain_evm::evm::EVMServices;
use jsonrpsee::{core::RpcResult, proc_macros::rpc};

use crate::errors::RPCError;

#[rpc(server, client, namespace = "net")]
pub trait MetachainNetRPC {
    /// Returns the current network ID as a string.
    #[method(name = "version")]
    fn net_version(&self) -> RpcResult<String>;

    #[method(name = "peerCount")]
    fn peer_count(&self) -> RpcResult<String>;
}

pub struct MetachainNetRPCModule {
    _handler: Arc<EVMServices>,
}

impl MetachainNetRPCModule {
    #[must_use]
    pub fn new(handler: Arc<EVMServices>) -> Self {
        Self { _handler: handler }
    }
}

impl MetachainNetRPCServer for MetachainNetRPCModule {
    fn net_version(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id().map_err(RPCError::Error)?;
        Ok(format!("{chain_id}"))
    }

    fn peer_count(&self) -> RpcResult<String> {
        let peer_count = ain_cpp_imports::get_num_connections();

        Ok(format!("{:#x}", peer_count))
    }
}
