use ethereum::{EIP658ReceiptData, Log, ReceiptV3};
use ethereum_types::{Bloom, U256};
use evm::{
    backend::{ApplyBackend, Backend},
    executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata},
    Config, CreateScheme, ExitReason,
};
use log::trace;
use primitive_types::{H160, H256};

use crate::bytes::Bytes;
use crate::precompiles::MetachainPrecompiles;
use crate::Result;
use crate::{
    backend::EVMBackend,
    core::EVMCoreService,
    traits::{BridgeBackend, Executor, ExecutorContext},
    transaction::SignedTx,
};

pub struct AinExecutor<'backend> {
    pub backend: &'backend mut EVMBackend,
}

impl<'backend> AinExecutor<'backend> {
    pub fn new(backend: &'backend mut EVMBackend) -> Self {
        Self { backend }
    }

    pub fn add_balance(&mut self, address: H160, amount: U256) -> Result<()> {
        self.backend.add_balance(address, amount)
    }

    pub fn sub_balance(&mut self, address: H160, amount: U256) -> Result<()> {
        self.backend.sub_balance(address, amount)
    }

    pub fn deploy_contract(
        &mut self,
        address: H160,
        bytecode: Bytes,
        storage: Vec<(H256, H256)>,
    ) -> Result<()> {
        self.backend.deploy_contract(&address, bytecode.0, storage)
    }

    pub fn update_storage(&mut self, address: H160, storage: Vec<(H256, H256)>) -> Result<()> {
        self.backend.update_storage(&address, storage)
    }

    pub fn commit(&mut self) -> H256 {
        self.backend.commit()
    }

    pub fn get_nonce(&self, address: &H160) -> U256 {
        self.backend.get_nonce(address)
    }
}

impl<'backend> Executor for AinExecutor<'backend> {
    const CONFIG: Config = Config::shanghai();

    /// Read-only call
    fn call(&mut self, ctx: ExecutorContext) -> TxResponse {
        let metadata = StackSubstateMetadata::new(ctx.gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata, self.backend);
        let precompiles = MetachainPrecompiles;
        let mut executor = StackExecutor::new_with_precompiles(state, &Self::CONFIG, &precompiles);
        let access_list = ctx
            .access_list
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();

        let (exit_reason, data) = match ctx.to {
            Some(address) => executor.transact_call(
                ctx.caller,
                address,
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                access_list,
            ),
            None => {
                let contract_address =
                    executor.create_address(CreateScheme::Legacy { caller: ctx.caller });
                let (exit_reason, _) = executor.transact_create(
                    ctx.caller,
                    ctx.value,
                    ctx.data.to_vec(),
                    ctx.gas_limit,
                    access_list,
                );
                let code = executor.state().code(contract_address);
                (exit_reason, code)
            }
        };

        TxResponse {
            exit_reason,
            data,
            logs: Vec::new(),
            used_gas: executor.used_gas(),
        }
    }

    /// Update state
    fn exec(&mut self, signed_tx: &SignedTx, prepay_gas: U256) -> (TxResponse, ReceiptV3) {
        self.backend.update_vicinity_from_tx(signed_tx);
        trace!(
            "[Executor] Executing EVM TX with vicinity : {:?}",
            self.backend.vicinity
        );
        let ctx = ExecutorContext {
            caller: signed_tx.sender,
            to: signed_tx.to(),
            value: signed_tx.value(),
            data: signed_tx.data(),
            gas_limit: signed_tx.gas_limit().as_u64(),
            access_list: signed_tx.access_list(),
        };

        self.backend.deduct_prepay_gas(signed_tx.sender, prepay_gas);

        let metadata = StackSubstateMetadata::new(ctx.gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata, self.backend);
        let precompiles = MetachainPrecompiles;
        let mut executor = StackExecutor::new_with_precompiles(state, &Self::CONFIG, &precompiles);
        let access_list = ctx
            .access_list
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();

        let (exit_reason, data) = match ctx.to {
            Some(address) => executor.transact_call(
                ctx.caller,
                address,
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                access_list,
            ),
            None => executor.transact_create(
                ctx.caller,
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                access_list,
            ),
        };

        let used_gas = executor.used_gas();
        let (values, logs) = executor.into_state().deconstruct();
        let logs = logs.into_iter().collect::<Vec<_>>();

        ApplyBackend::apply(self.backend, values, logs.clone(), true);
        self.backend.commit();

        if prepay_gas != U256::zero() {
            self.backend.refund_unused_gas(
                signed_tx.sender,
                signed_tx.gas_limit(),
                U256::from(used_gas),
                signed_tx.gas_price(),
            );
        }

        let receipt = ReceiptV3::EIP1559(EIP658ReceiptData {
            logs_bloom: {
                let mut bloom: Bloom = Bloom::default();
                EVMCoreService::logs_bloom(logs.clone(), &mut bloom);
                bloom
            },
            status_code: u8::from(exit_reason.is_succeed()),
            logs: logs.clone(),
            used_gas: U256::from(used_gas),
        });

        (
            TxResponse {
                exit_reason,
                data,
                logs,
                used_gas,
            },
            receipt,
        )
    }
}

#[derive(Debug)]
pub struct TxResponse {
    pub exit_reason: ExitReason,
    pub data: Vec<u8>,
    pub logs: Vec<Log>,
    pub used_gas: u64,
}
