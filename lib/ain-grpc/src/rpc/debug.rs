use std::{collections::BTreeMap, sync::Arc};

use ain_evm::{
    core::EthCallArgs,
    evm::EVMServices,
    executor::TxResponse,
    storage::traits::{BlockStorage, ReceiptStorage, TransactionStorage},
    trace::types::single::TransactionTrace,
    transaction::SignedTx,
};

use ethereum_types::{H160, H256, U256};
use jsonrpsee::{
    core::{JsonValue, RpcResult},
    proc_macros::rpc,
};
use log::debug;
use serde_json::json;

use crate::{
    block::BlockNumber,
    call_request::{override_to_overlay, CallRequest, CallStateOverride},
    errors::{to_custom_err, RPCError},
    trace::{handle_trace_params, TraceParams},
};

use super::common::get_block;

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
    fn trace_transaction(
        &self,
        tx_hash: H256,
        trace_params: Option<TraceParams>,
    ) -> RpcResult<TransactionTrace>;

    #[method(name = "traceCall")]
    fn trace_call(
        &self,
        call: CallRequest,
        block_number: BlockNumber,
        trace_params: Option<TraceParams>,
        state_overrides: Option<BTreeMap<H160, CallStateOverride>>,
    ) -> RpcResult<TransactionTrace>;

    #[method(name = "traceBlockByNumber")]
    fn trace_block_by_number(
        &self,
        block_number: BlockNumber,
        trace_params: Option<TraceParams>,
    ) -> RpcResult<Vec<JsonValue>>;

    #[method(name = "traceBlockByHash")]
    fn trace_block_by_hash(
        &self,
        hash: H256,
        trace_params: Option<TraceParams>,
    ) -> RpcResult<Vec<JsonValue>>;

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
    /// Replays a transaction in the Runtime at a given block height.
    /// In order to succesfully reproduce the result of the original transaction we need a correct
    /// state to replay over.
    fn trace_transaction(
        &self,
        tx_hash: H256,
        trace_params: Option<TraceParams>,
    ) -> RpcResult<TransactionTrace> {
        self.is_trace_enabled().or_else(|_| self.is_enabled())?;

        // Handle trace params
        let params = handle_trace_params(trace_params)?;
        let raw_max_memory_usage =
            usize::try_from(ain_cpp_imports::get_tracing_raw_max_memory_usage_bytes())
                .map_err(|_| to_custom_err("failed to convert response size limit to usize"))?;

        // Get signed tx
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

        Ok(self
            .handler
            .tracer
            .trace_transaction(
                &signed_tx,
                receipt.block_number,
                params,
                raw_max_memory_usage,
            )
            .map_err(RPCError::EvmError)?)
    }

    fn trace_call(
        &self,
        call: CallRequest,
        block_number: BlockNumber,
        trace_params: Option<TraceParams>,
        state_overrides: Option<BTreeMap<H160, CallStateOverride>>,
    ) -> RpcResult<TransactionTrace> {
        self.is_trace_enabled().or_else(|_| self.is_enabled())?;

        // Handle trace params
        let params = handle_trace_params(trace_params)?;
        let raw_max_memory_usage =
            usize::try_from(ain_cpp_imports::get_tracing_raw_max_memory_usage_bytes())
                .map_err(|_| to_custom_err("failed to convert response size limit to usize"))?;

        // Get call arguments
        let caller = call.from.unwrap_or_default();
        let byte_data = call.get_data()?;
        let data = byte_data.0.as_slice();

        // Get gas
        let block_gas_limit = ain_cpp_imports::get_attribute_values(None).block_gas_limit;
        let gas_limit = u64::try_from(call.gas.unwrap_or(U256::from(block_gas_limit)))
            .map_err(to_custom_err)?;

        let block = get_block(&self.handler.storage, Some(block_number))?;
        let block_base_fee = block.header.base_fee;
        let gas_price = call.get_effective_gas_price()?.unwrap_or(block_base_fee);

        Ok(self
            .handler
            .tracer
            .trace_call(
                EthCallArgs {
                    caller,
                    to: call.to,
                    value: call.value.unwrap_or_default(),
                    data,
                    gas_limit,
                    gas_price,
                    access_list: call.access_list.unwrap_or_default(),
                    block_number: block.header.number,
                },
                state_overrides.map(override_to_overlay),
                params,
                raw_max_memory_usage,
            )
            .map_err(RPCError::EvmError)?)
    }

    fn trace_block_by_number(
        &self,
        block_number: BlockNumber,
        trace_params: Option<TraceParams>,
    ) -> RpcResult<Vec<JsonValue>> {
        self.is_trace_enabled().or_else(|_| self.is_enabled())?;

        // Handle trace params
        let params = handle_trace_params(trace_params)?;
        let raw_max_memory_usage =
            usize::try_from(ain_cpp_imports::get_tracing_raw_max_memory_usage_bytes())
                .map_err(|_| to_custom_err("failed to convert response size limit to usize"))?;

        // Get block
        let trace_block = get_block(&self.handler.storage, Some(block_number))?;
        let res = self
            .handler
            .tracer
            .trace_block(trace_block, params, raw_max_memory_usage)
            .map_err(RPCError::EvmError)?
            .into_iter()
            .map(|(tx_hash, trace)| json!({ "txHash": format!("{:?}", tx_hash), "result": trace }))
            .collect();
        Ok(res)
    }

    fn trace_block_by_hash(
        &self,
        hash: H256,
        trace_params: Option<TraceParams>,
    ) -> RpcResult<Vec<JsonValue>> {
        self.is_trace_enabled().or_else(|_| self.is_enabled())?;

        // Handle trace params
        let params = handle_trace_params(trace_params)?;
        let raw_max_memory_usage =
            usize::try_from(ain_cpp_imports::get_tracing_raw_max_memory_usage_bytes())
                .map_err(|_| to_custom_err("failed to convert response size limit to usize"))?;

        // Get block
        let trace_block = self
            .handler
            .storage
            .get_block_by_hash(&hash)
            .map_err(to_custom_err)?
            .ok_or(RPCError::BlockNotFound)?;
        let res = self
            .handler
            .tracer
            .trace_block(trace_block, params, raw_max_memory_usage)
            .map_err(RPCError::EvmError)?
            .into_iter()
            .map(|(tx_hash, trace)| json!({ "txHash": format!("{:?}", tx_hash), "result": trace }))
            .collect();
        Ok(res)
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
        let gas_price = call.get_effective_gas_price()?.unwrap_or(block_base_fee);

        let TxResponse { used_gas, .. } = self
            .handler
            .core
            .call(
                EthCallArgs {
                    caller,
                    to: call.to,
                    value: call.value.unwrap_or_default(),
                    data,
                    gas_limit,
                    gas_price,
                    access_list: call.access_list.unwrap_or_default(),
                    block_number,
                },
                None,
            )
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
}
