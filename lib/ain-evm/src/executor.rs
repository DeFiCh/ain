use crate::{
    backend::{EVMBackend, EVMBackendError},
    evm::EVMHandler,
    traits::{BridgeBackend, Executor, ExecutorContext},
    transaction::SignedTx,
};
use ethereum::{EIP658ReceiptData, Log, ReceiptV3};
use ethereum_types::{Bloom, U256};
use evm::{
    backend::ApplyBackend,
    executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata},
    Config, ExitReason,
};
use log::trace;
use primitive_types::{H160, H256};
use std::collections::BTreeMap;

pub struct AinExecutor<'backend> {
    backend: &'backend mut EVMBackend,
}

impl<'backend> AinExecutor<'backend> {}

impl<'backend> AinExecutor<'backend> {
    pub fn new(backend: &'backend mut EVMBackend) -> Self {
        Self { backend }
    }

    pub fn add_balance(&mut self, address: H160, amount: U256) -> Result<(), EVMBackendError> {
        self.backend.add_balance(address, amount)
    }

    pub fn sub_balance(&mut self, address: H160, amount: U256) -> Result<(), EVMBackendError> {
        self.backend.sub_balance(address, amount)
    }

    pub fn commit(&mut self) -> H256 {
        self.backend.commit()
    }
}

impl<'backend> Executor for AinExecutor<'backend> {
    const CONFIG: Config = Config::london();

    fn call(&mut self, ctx: ExecutorContext, apply: bool) -> TxResponse {
        let metadata = StackSubstateMetadata::new(ctx.gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata, self.backend);
        let precompiles = BTreeMap::new(); // TODO Add precompile crate
        let mut executor = StackExecutor::new_with_precompiles(state, &Self::CONFIG, &precompiles);
        let access_list = ctx
            .access_list
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();
        let (exit_reason, data) = match ctx.to {
            Some(address) => executor.transact_call(
                ctx.caller.unwrap_or_default(),
                address,
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                access_list,
            ),
            None => executor.transact_create(
                ctx.caller.unwrap_or_default(),
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                access_list,
            ),
        };

        let used_gas = executor.used_gas();
        let (values, logs) = executor.into_state().deconstruct();
        let logs = logs.into_iter().collect::<Vec<_>>();
        if apply && exit_reason.is_succeed() {
            ApplyBackend::apply(self.backend, values, logs.clone(), true);
        }

        let receipt = ReceiptV3::EIP1559(EIP658ReceiptData {
            logs_bloom: {
                let mut bloom: Bloom = Bloom::default();
                EVMHandler::logs_bloom(logs.clone(), &mut bloom);
                bloom
            },
            status_code: u8::from(exit_reason.is_succeed()),
            logs: logs.clone(),
            used_gas: U256::from(used_gas),
        });

        TxResponse {
            exit_reason,
            data,
            logs,
            used_gas,
            receipt,
        }
    }

    fn exec(&mut self, signed_tx: &SignedTx) -> TxResponse {
        let apply = true;
        self.backend.update_vicinity_from_tx(signed_tx);
        trace!(
            "[Executor] Executing EVM TX with vicinity : {:?}",
            self.backend.vicinity
        );
        self.call(
            ExecutorContext {
                caller: Some(signed_tx.sender),
                to: signed_tx.to(),
                value: signed_tx.value(),
                data: signed_tx.data(),
                gas_limit: signed_tx.gas_limit().as_u64(),
                access_list: signed_tx.access_list(),
            },
            apply,
        )
    }
}

#[derive(Debug)]
pub struct TxResponse {
    pub exit_reason: ExitReason,
    pub data: Vec<u8>,
    pub logs: Vec<Log>,
    pub used_gas: u64,
    pub receipt: ReceiptV3,
}
