use ain_evm::evm::EVMServices;
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use std::sync::Arc;

#[rpc(server, client, namespace = "net")]
pub trait MetachainNetRPC {
    /// Returns the current network ID as a string.
    #[method(name = "version")]
    fn net_version(&self) -> RpcResult<String>;
}

pub struct MetachainNetRPCModule {
    handler: Arc<EVMServices>,
}

impl MetachainNetRPCModule {
    #[must_use]
    pub fn new(handler: Arc<EVMServices>) -> Self {
        Self { handler }
    }
}

impl MetachainNetRPCServer for MetachainNetRPCModule {
    fn net_version(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {e:?}")))?;

        Ok(format!("{chain_id}"))
    }
}
