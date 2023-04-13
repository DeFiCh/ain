use std::collections::BTreeMap;

use crate::{traits::Executor, transaction::SignedTx};
use evm::{
    backend::{ApplyBackend, Backend},
    executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata},
    Config, ExitReason,
};

use ethereum::{AccessList, Log};

use primitive_types::{H160, U256};

#[derive(Debug)]
pub struct AinExecutor<B: Backend> {
    backend: B,
}

impl<B> AinExecutor<B>
where
    B: Backend + ApplyBackend,
{
    pub fn new(backend: B) -> Self {
        Self { backend }
    }

    pub fn backend(&self) -> &B {
        &self.backend
    }
}

impl<B> Executor for AinExecutor<B>
where
    B: Backend + ApplyBackend,
{
    const CONFIG: Config = Config::london();

    fn call(
        &mut self,
        caller: Option<H160>,
        to: Option<H160>,
        value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
        apply: bool,
    ) -> TxResponse {
        let metadata = StackSubstateMetadata::new(gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata, &self.backend);
        let precompiles = BTreeMap::new(); // TODO Add precompile crate
        let mut executor = StackExecutor::new_with_precompiles(state, &Self::CONFIG, &precompiles);
        let access_list = access_list
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();
        let (exit_reason, data) = match to {
            Some(address) => executor.transact_call(
                caller.unwrap_or_default(),
                address,
                value,
                data.to_vec(),
                gas_limit,
                access_list,
            ),
            None => executor.transact_create(
                caller.unwrap_or_default(),
                value,
                data.to_vec(),
                gas_limit,
                access_list,
            ),
        };

        let (values, logs) = executor.into_state().deconstruct();
        let logs = logs.into_iter().collect::<Vec<_>>();
        if apply && exit_reason.is_succeed() {
            self.backend.apply(values, logs.clone(), true);
        }

        TxResponse {
            exit_reason,
            data,
            logs,
        }
    }

    fn exec(&mut self, signed_tx: &SignedTx) -> TxResponse {
        let apply = true;
        self.call(
            Some(signed_tx.sender),
            signed_tx.to(),
            signed_tx.value(),
            signed_tx.data(),
            signed_tx.gas_limit().as_u64(),
            signed_tx.access_list(),
            apply,
        )
    }
}

#[derive(Debug)]
pub struct TxResponse {
    pub exit_reason: ExitReason,
    pub data: Vec<u8>,
    pub logs: Vec<Log>,
}
