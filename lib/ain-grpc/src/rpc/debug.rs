use std::{cmp, sync::Arc};

use ain_evm::storage::traits::{ReceiptStorage, TransactionStorage};
use ain_evm::transaction::SignedTx;
use ain_evm::{
    core::EthCallArgs, evm::EVMServices, executor::TxResponse, storage::block_store::DumpArg,
};
use ethereum::Account;
use ethereum_types::{H256, U256};
use jsonrpsee::{
    core::{Error, RpcResult},
    proc_macros::rpc,
};
use log::debug;
use rlp::{Decodable, Rlp};

use super::to_jsonrpsee_custom_error;
use crate::call_request::CallRequest;
use crate::transaction::{TraceLogs, TraceTransactionResult};

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
    fn trace_transaction(&self, tx_hash: H256) -> RpcResult<TraceTransactionResult>;

    // Dump full db
    #[method(name = "dumpdb")]
    fn dump_db(
        &self,
        arg: Option<DumpArg>,
        from: Option<&str>,
        limit: Option<&str>,
    ) -> RpcResult<()>;

    // Log accounts state
    #[method(name = "logaccountstates")]
    fn log_account_states(&self) -> RpcResult<()>;

    // Log block template state
    #[method(name = "logblocktemplates")]
    fn log_block_templates(&self) -> RpcResult<()>;

    // Get transaction fee estimate
    #[method(name = "feeEstimate")]
    fn fee_estimate(&self, input: CallRequest) -> RpcResult<FeeEstimate>;
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
    fn trace_transaction(&self, tx_hash: H256) -> RpcResult<TraceTransactionResult> {
        debug!(target: "rpc", "Tracing transaction {tx_hash}");

        let receipt = self
            .handler
            .storage
            .get_receipt(&tx_hash)
            .map_err(to_jsonrpsee_custom_error)?
            .ok_or_else(|| Error::Custom(format!("Error")))?;

        let tx = self
            .handler
            .storage
            .get_transaction_by_block_hash_and_index(&receipt.block_hash, receipt.tx_index)
            .expect("Unable to find TX hash")
            .ok_or_else(|| Error::Custom("Error".to_string()))?;

        let signed_tx = SignedTx::try_from(tx).expect("Unable to construct signed TX");

        let (logs, succeeded, return_data, gas_used) = self
            .handler
            .core
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

    fn dump_db(
        &self,
        arg: Option<DumpArg>,
        start: Option<&str>,
        limit: Option<&str>,
    ) -> RpcResult<()> {
        let default_limit = 100usize;
        let limit = limit
            .map_or(Ok(default_limit), |s| s.parse())
            .map_err(|e| Error::Custom(e.to_string()))?;
        self.handler
            .storage
            .dump_db(arg.unwrap_or(DumpArg::All), start, limit);
        Ok(())
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

    fn fee_estimate(&self, input: CallRequest) -> RpcResult<FeeEstimate> {
        let CallRequest {
            from,
            to,
            gas,
            value,
            data,
            access_list,
            transaction_type,
            gas_price,
            max_fee_per_gas,
            max_priority_fee_per_gas,
            ..
        } = input;

        let (block_hash, block_number) = self
            .handler
            .block
            .get_latest_block_hash_and_number()
            .map_err(to_jsonrpsee_custom_error)?
            .ok_or(Error::Custom(
                "Error fetching latest block hash and number".to_string(),
            ))?;
        let base_fee = self
            .handler
            .block
            .calculate_base_fee(block_hash)
            .map_err(to_jsonrpsee_custom_error)?;
        let Ok(gas_limit) = u64::try_from(gas.ok_or(Error::Custom(
            "Cannot get fee estimate without specifying gas limit".to_string(),
        ))?) else {
            return Err(Error::Custom(
                "Cannot get fee estimate, gas value overflow".to_string(),
            ));
        };

        let TxResponse { used_gas, .. } = self
            .handler
            .core
            .call(EthCallArgs {
                caller: from,
                to,
                value: value.unwrap_or_default(),
                data: &data.map(|d| d.0).unwrap_or_default(),
                gas_limit,
                gas_price,
                max_fee_per_gas,
                access_list: access_list.unwrap_or_default(),
                block_number,
                transaction_type,
            })
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;

        let used_gas = U256::from(used_gas);
        let gas_fee = match transaction_type {
            // Legacy
            None => {
                let Some(gas_price) = gas_price else {
                    return Err(Error::Custom(
                        "Cannot get Legacy TX fee estimate without gas price".to_string()
                    ));
                };
                used_gas.checked_mul(gas_price)
            }
            // EIP2930
            Some(typ) if typ == U256::one() => {
                let Some(gas_price) = gas_price else {
                    return Err(Error::Custom(
                        "Cannot get EIP2930 TX fee estimate without gas price".to_string()
                    ));
                };
                used_gas.checked_mul(gas_price)
            }
            // EIP1559
            Some(typ) if typ == U256::from(2) => {
                let (Some(max_fee_per_gas), Some(max_priority_fee_per_gas)) =
                    (max_fee_per_gas, max_priority_fee_per_gas)
                else {
                    return Err(Error::Custom("Cannot get EIP1559 TX fee estimate without max_fee_per_gas and max_priority_fee_per_gas".to_string()));
                };
                let gas_fee = cmp::min(max_fee_per_gas, max_priority_fee_per_gas.checked_add(base_fee).ok_or_else(|| Error::Custom("max_priority_fee_per_gas overflow".to_string()))?);
                used_gas.checked_mul(gas_fee)
            }
            _ => {
                return Err(Error::Custom(
                    "Wrong transaction type. Should be either None, 1 or 2".to_string()
                ))
            }
        }.ok_or(Error::Custom(
            "Cannot get fee estimate, fee value overflow".to_string()
        ))?;

        let burnt_fee = used_gas
            .checked_mul(base_fee)
            .ok_or_else(|| Error::Custom("burnt_fee overflow".to_string()))?;
        let priority_fee = gas_fee
            .checked_sub(burnt_fee)
            .ok_or_else(|| Error::Custom("priority_fee underflow".to_string()))?;

        Ok(FeeEstimate {
            used_gas,
            gas_fee,
            burnt_fee,
            priority_fee,
        })
    }

    fn log_block_templates(&self) -> RpcResult<()> {
        let templates = &self.handler.core.block_templates;
        debug!("templates : {:#?}", templates);
        Ok(())
    }
}
