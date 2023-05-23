use ain_evm::handler::Handlers;
use jsonrpsee::core::RpcResult;
use jsonrpsee::proc_macros::rpc;
use log::debug;
use std::sync::Arc;

#[rpc(server, client, namespace = "debug")]
pub trait MetachainDebugRPC {
    #[method(name = "traceTransaction")]
    fn trace_transaction(&self) -> RpcResult<()>;

    // Dump full db
    #[method(name = "dumpdb")]
    fn dump_db(&self) -> RpcResult<()>;
}

pub struct MetachainDebugRPCModule {
    handler: Arc<Handlers>,
}

impl MetachainDebugRPCModule {
    #[must_use]
    pub fn new(handler: Arc<Handlers>) -> Self {
        Self { handler }
    }
}

impl MetachainDebugRPCServer for MetachainDebugRPCModule {
    fn trace_transaction(&self) -> RpcResult<()> {
        debug!(target: "rpc", "Tracing transaction");
        Ok(())
    }

    fn dump_db(&self) -> RpcResult<()> {
        self.handler.storage.dump_db();
        Ok(())
    }
}
