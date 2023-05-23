use ain_evm::handler::Handlers;
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use log::debug;
use std::sync::Arc;
use primitive_types::H256;
use ain_evm::evm::ExecutionStep;
use ain_evm::executor::TxResponse;
use ain_evm::storage::traits::{BlockStorage, ReceiptStorage, TransactionStorage};
use ain_evm::transaction::SignedTx;
use crate::codegen::types::EthTransactionInfo;
use crate::transaction::TraceLogs;

#[rpc(server, client, namespace = "debug")]
pub trait MetachainDebugRPC {
    #[method(name = "traceTransaction")]
    fn trace_transaction(&self, tx_hash: H256) -> RpcResult<Vec<ExecutionStep>>;

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
    fn trace_transaction(&self, tx_hash: H256) -> RpcResult<Vec<ExecutionStep>> {
        debug!(target: "rpc", "Tracing transaction {tx_hash}");

        let receipt = self.handler.storage.get_receipt(&tx_hash).expect("Receipt not found");
        let tx = self.handler.storage.get_transaction_by_block_hash_and_index(&receipt.block_hash, receipt.tx_index).expect("Unable to find TX hash");

        let signed_tx = SignedTx::try_from(tx).expect("Unable to construct signed TX");

        let logs = self
            .handler
            .evm
            .trace_transaction(
                signed_tx.sender,
                signed_tx.to().unwrap(),
                signed_tx.value(),
                signed_tx.data(),
                signed_tx.gas_limit().as_u64(),
                signed_tx.access_list(),
                receipt.block_number
            )
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;

        Ok(logs)
    }

    fn dump_db(&self) -> RpcResult<()> {
        self.handler.storage.dump_db();
        Ok(())
    }
}
