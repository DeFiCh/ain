use crate::call_request::CallRequest;
use ain_evm::core::MAX_GAS_PER_BLOCK;
use ain_evm::{core::EthCallArgs, evm::EVMServices, executor::TxResponse};

use ethereum::Account;
use ethereum_types::U256;
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use log::debug;
use rlp::{Decodable, Rlp};
use std::cmp;
use std::sync::Arc;

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
    fn dump_db(&self) -> RpcResult<()>;

    // Log accounts state
    #[method(name = "logaccountstates")]
    fn log_account_states(&self) -> RpcResult<()>;

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
            .core
            .get_latest_block_backend()
            .expect("Error restoring backend");
        let ro_handle = backend.ro_handle();

        ro_handle.iter().for_each(|el| match el {
            Ok((_, v)) => {
                if let Ok(account) = Account::decode(&Rlp::new(&v)) {
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
            .ok_or(Error::Custom(
                "Error fetching latest block hash and number".to_string(),
            ))?;
        let base_fee = self.handler.block.calculate_base_fee(block_hash);

        let TxResponse { used_gas, .. } = self
            .handler
            .core
            .call(EthCallArgs {
                caller: from,
                to,
                value: value.unwrap_or_default(),
                data: &data.map(|d| d.0).unwrap_or_default(),
                gas_limit: gas.unwrap_or(MAX_GAS_PER_BLOCK).as_u64(),
                access_list: access_list.unwrap_or_default(),
                block_number,
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
                let gas_fee = cmp::min(max_fee_per_gas, max_priority_fee_per_gas + base_fee);
                used_gas.checked_mul(gas_fee)
            }
            _ => {
                return Err(Error::Custom(
                    "Wrong transaction type. Should be either None, 1 or 2".to_string()
                ))
            }
        }.ok_or(Error::Custom(
            "Overflow happened during fee calculation".to_string()
        ))?;

        let burnt_fee = used_gas * base_fee;
        let priority_fee = gas_fee - burnt_fee;
        Ok(FeeEstimate {
            used_gas,
            gas_fee,
            burnt_fee,
            priority_fee,
        })
    }
}
