use ain_evm::handler::Handlers;
use ethereum::Account;
use jsonrpsee::core::RpcResult;
use jsonrpsee::proc_macros::rpc;
use log::debug;
use rlp::{Decodable, Rlp};
use std::sync::Arc;

#[rpc(server, client, namespace = "debug")]
pub trait MetachainDebugRPC {
    #[method(name = "traceTransaction")]
    fn trace_transaction(&self) -> RpcResult<()>;

    // Dump full db
    #[method(name = "dumpdb")]
    fn dump_db(&self) -> RpcResult<()>;

    // Log accounts state
    #[method(name = "logaccountstates")]
    fn log_account_states(&self) -> RpcResult<()>;
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

    fn log_account_states(&self) -> RpcResult<()> {
        let backend = self
            .handler
            .evm
            .get_latest_block_backend()
            .expect("Error restoring backend");
        let ro_handle = backend.ro_handle();

        ro_handle.iter().for_each(|el| match el {
            Ok((_, v)) => {
                if let Some(account) = Account::decode(&Rlp::new(&v)).ok() {
                    debug!("[log_account_states] account {:?}", account)
                } else {
                    debug!("[log_account_states] Error decoding account {:?}", v)
                }
            }
            Err(e) => {
                debug!("[log_account_states] Error on iter element {e}")
            }
        });

        Ok(())
    }
}
