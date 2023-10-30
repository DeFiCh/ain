use std::sync::Arc;

use ain_evm::{
    core::EthCallArgs, evm::EVMServices, executor::TxResponse, storage::block_store::DumpArg,
};
use ethereum::Account;
use ethereum_types::U256;
use jsonrpsee::{
    core::{Error, RpcResult},
    proc_macros::rpc,
};
use log::debug;
use rlp::{Decodable, Rlp};

use crate::{
    call_request::CallRequest,
    errors::{to_custom_err, RPCError},
};

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
    fn trace_transaction(&self) -> RpcResult<()>;

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
}

impl MetachainDebugRPCServer for MetachainDebugRPCModule {
    fn trace_transaction(&self) -> RpcResult<()> {
        debug!(target: "rpc", "Tracing transaction");
        Ok(())
    }

    fn dump_db(
        &self,
        arg: Option<DumpArg>,
        start: Option<&str>,
        limit: Option<&str>,
    ) -> RpcResult<String> {
        let default_limit = 100usize;
        let limit = limit
            .map_or(Ok(default_limit), |s| s.parse())
            .map_err(|e| Error::Custom(e.to_string()))?;
        self.handler
            .storage
            .dump_db(arg.unwrap_or(DumpArg::All), start, limit)
            .map_err(to_custom_err)
    }

    fn log_account_states(&self) -> RpcResult<()> {
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
        debug!(target:"rpc",  "Fee estimate");
        let caller = call.from.ok_or(RPCError::NoSenderAddress)?;
        let byte_data = call.get_data()?;
        let data = byte_data.0.as_slice();

        // Get latest block information
        let (block_hash, block_number) = self
            .handler
            .block
            .get_latest_block_hash_and_number()
            .map_err(to_custom_err)?
            .unwrap_or_default();

        // Get gas
        let block_gas_limit = self
            .handler
            .storage
            .get_attributes_or_default()
            .map_err(to_custom_err)?
            .block_gas_limit;
        let gas_limit = u64::try_from(call.gas.unwrap_or(U256::from(block_gas_limit)))
            .map_err(to_custom_err)?;

        // Get gas price
        let block_base_fee = self
            .handler
            .block
            .calculate_base_fee(block_hash)
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
            .map_err(RPCError::EvmCall)?;

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
        // let templates = &self.handler.core.block_templates;
        // debug!("templates : {:#?}", templates);
        Ok(())
    }
}
