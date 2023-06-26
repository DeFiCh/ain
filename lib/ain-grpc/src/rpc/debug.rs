use crate::transaction::{TraceLogs, TraceTransactionResult};
use ain_evm::handler::Handlers;
use ain_evm::storage::traits::{ReceiptStorage, TransactionStorage};
use ain_evm::transaction::SignedTx;
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use log::debug;
use primitive_types::{H256, U256};
use std::sync::Arc;

#[rpc(server, client, namespace = "debug")]
pub trait MetachainDebugRPC {
    #[method(name = "traceTransaction")]
    fn trace_transaction(&self, tx_hash: H256) -> RpcResult<TraceTransactionResult>;

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
    fn trace_transaction(&self, tx_hash: H256) -> RpcResult<TraceTransactionResult> {
        debug!(target: "rpc", "Tracing transaction {tx_hash}");

        let receipt = self
            .handler
            .storage
            .get_receipt(&tx_hash)
            .expect("Receipt not found");
        let tx = self
            .handler
            .storage
            .get_transaction_by_block_hash_and_index(&receipt.block_hash, receipt.tx_index)
            .expect("Unable to find TX hash");

        let signed_tx = SignedTx::try_from(tx).expect("Unable to construct signed TX");

        let (logs, succeeded, return_data, gas_used) = self
            .handler
            .evm
            .trace_transaction(
                signed_tx.sender,
                signed_tx.to(),
                signed_tx.value(),
                signed_tx.data(),
                signed_tx.gas_limit().as_u64(),
                signed_tx.access_list(),
                receipt.block_number,
            )
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;

        let trace_logs = logs.iter().map(|x| TraceLogs::from(x.clone())).collect();

        Ok(TraceTransactionResult {
            gas: U256::from(gas_used),
            failed: !succeeded,
            return_value: format!("{}", hex::encode(return_data)),
            struct_logs: trace_logs,
        })
    }

    fn dump_db(&self) -> RpcResult<()> {
        self.handler.storage.dump_db();
        Ok(())
    }
}
