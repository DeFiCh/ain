use std::sync::Arc;

use ain_evm::{
    core::EthCallArgs,
    evm::EVMServices,
    executor::TxResponse,
    storage::{
        block_store::DumpArg,
        traits::{ReceiptStorage, TransactionStorage},
    },
    transaction::SignedTx,
};
use ethereum::Account;
use ethereum_types::{H256, U256};
use jsonrpsee::{core::RpcResult, proc_macros::rpc};
use log::debug;
use rlp::{Decodable, Rlp};

use crate::{
    call_request::CallRequest,
    errors::{to_custom_err, RPCError},
    transaction::{TraceLogs, TraceTransactionResult},
};
use crate::tracers::Tracer;

#[derive(Serialize, Deserialize)]
pub struct FeeEstimate {
    used_gas: U256,
    gas_fee: U256,
    burnt_fee: U256,
    priority_fee: U256,
}

#[rpc(server, client, namespace = "debug")]
pub trait MetachainDebugRPC {
    #[method(name = "traceTransaction")]
    fn trace_transaction(&self, tx_hash: H256, tracer: Option<Tracer>) -> RpcResult<TraceTransactionResult>;

    // Dump full db
    #[method(name = "dumpdb")]
    fn dump_db(
        &self,
        arg: Option<DumpArg>,
        from: Option<&str>,
        limit: Option<&str>,
    ) -> RpcResult<String>;

    // Log accounts state
    #[method(name = "logaccountstates")]
    fn log_account_states(&self) -> RpcResult<()>;

    // Log block template state
    #[method(name = "logblocktemplates")]
    fn log_block_templates(&self) -> RpcResult<()>;

    // Get transaction fee estimate
    #[method(name = "feeEstimate")]
    fn fee_estimate(&self, call: CallRequest) -> RpcResult<FeeEstimate>;
}

pub struct MetachainDebugRPCModule {
    handler: Arc<EVMServices>,
}

impl MetachainDebugRPCModule {
    #[must_use]
    pub fn new(handler: Arc<EVMServices>) -> Self {
        Self { handler }
    }

    fn is_enabled(&self) -> RpcResult<()> {
        if !ain_cpp_imports::is_eth_debug_rpc_enabled() {
            return Err(RPCError::DebugNotEnabled.into());
        }
        Ok(())
    }

    fn is_trace_enabled(&self) -> RpcResult<()> {
        if !ain_cpp_imports::is_eth_debug_trace_rpc_enabled() {
            return Err(RPCError::TraceNotEnabled.into());
        }
        Ok(())
    }
}

impl MetachainDebugRPCServer for MetachainDebugRPCModule {
    fn trace_transaction(&self, tx_hash: H256, tracer: Option<Tracer>) -> RpcResult<TraceTransactionResult> {
        debug!("{tracer:?}");
        self.is_trace_enabled().or_else(|_| self.is_enabled())?;

        debug!(target: "rpc", "Tracing transaction {tx_hash}");

        let receipt = self
            .handler
            .storage
            .get_receipt(&tx_hash)
            .map_err(to_custom_err)?
            .ok_or(RPCError::ReceiptNotFound(tx_hash))?;

        let tx = self
            .handler
            .storage
            .get_transaction_by_block_hash_and_index(&receipt.block_hash, receipt.tx_index)
            .map_err(RPCError::EvmError)?
            .ok_or(RPCError::TxNotFound(tx_hash))?;

        let signed_tx = SignedTx::try_from(tx).map_err(to_custom_err)?;
        let (logs, succeeded, return_data, gas_used) = self
            .handler
            .core
            .trace_transaction(&signed_tx, receipt.block_number)
            .map_err(RPCError::EvmError)?;
        let trace_logs = logs.iter().map(|x| TraceLogs::from(x.clone())).collect();

        Ok(TraceTransactionResult {
            gas: U256::from(gas_used),
            failed: !succeeded,
            return_value: hex::encode(return_data).to_string(),
            struct_logs: trace_logs,
        })
    }

    fn dump_db(
        &self,
        arg: Option<DumpArg>,
        start: Option<&str>,
        limit: Option<&str>,
    ) -> RpcResult<String> {
        self.is_enabled()?;

        let default_limit = 100usize;
        let limit = limit
            .map_or(Ok(default_limit), |s| s.parse())
            .map_err(to_custom_err)?;
        self.handler
            .storage
            .dump_db(arg.unwrap_or(DumpArg::All), start, limit)
            .map_err(to_custom_err)
    }

    fn log_account_states(&self) -> RpcResult<()> {
        self.is_enabled()?;

        let backend = self
            .handler
            .core
            .get_latest_block_backend()
            .expect("Error restoring backend");
        let ro_handle = backend.ro_handle();

        ro_handle.iter().for_each(|el| match el {
            Ok((_, v)) => {
                if let Ok(account) = Account::decode(&Rlp::new(&v)) {
                    debug!("[log_account_states] account {:?}", account);
                } else {
                    debug!("[log_account_states] Error decoding account {:?}", v);
                }
            }
            Err(e) => {
                debug!("[log_account_states] Error on iter element {e}");
            }
        });

        Ok(())
    }

    fn fee_estimate(&self, call: CallRequest) -> RpcResult<FeeEstimate> {
        self.is_enabled()?;

        debug!(target:"rpc",  "Fee estimate");
        let caller = call.from.unwrap_or_default();
        let byte_data = call.get_data()?;
        let data = byte_data.0.as_slice();
        let attrs = ain_cpp_imports::get_attribute_values(None);

        // Get latest block information
        let (block_hash, block_number) = self
            .handler
            .block
            .get_latest_block_hash_and_number()
            .map_err(to_custom_err)?
            .unwrap_or_default();

        let block_gas_target_factor = attrs.block_gas_target_factor;
        let block_gas_limit = attrs.block_gas_limit;

        let gas_limit = u64::try_from(call.gas.unwrap_or(U256::from(block_gas_limit)))
            .map_err(to_custom_err)?;

        // Get gas price
        let block_base_fee = self
            .handler
            .block
            .calculate_base_fee(block_hash, block_gas_target_factor)
            .map_err(to_custom_err)?;
        let gas_price = call.get_effective_gas_price(block_base_fee)?;

        let TxResponse { used_gas, .. } = self
            .handler
            .core
            .call(EthCallArgs {
                caller,
                to: call.to,
                value: call.value.unwrap_or_default(),
                data,
                gas_limit,
                gas_price,
                access_list: call.access_list.unwrap_or_default(),
                block_number,
            })
            .map_err(RPCError::EvmError)?;

        let used_gas = U256::from(used_gas);
        let gas_fee = used_gas
            .checked_mul(gas_price)
            .ok_or(RPCError::ValueOverflow)?;
        let burnt_fee = used_gas
            .checked_mul(block_base_fee)
            .ok_or(RPCError::ValueOverflow)?;
        let priority_fee = gas_fee
            .checked_sub(burnt_fee)
            .ok_or(RPCError::ValueOverflow)?;

        Ok(FeeEstimate {
            used_gas,
            gas_fee,
            burnt_fee,
            priority_fee,
        })
    }

    fn log_block_templates(&self) -> RpcResult<()> {
        self.is_enabled()?;

        // let templates = &self.handler.core.block_templates;
        // debug!("templates : {:#?}", templates);
        Ok(())
    }
}
