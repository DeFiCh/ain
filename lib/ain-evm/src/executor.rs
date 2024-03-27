use std::cell::RefCell;

use ain_contracts::{get_transfer_domain_contract, FixedContract};
use anyhow::format_err;
use ethereum::{AccessList, AccessListItem, EIP658ReceiptData, Log, ReceiptV3};
use ethereum_types::{Bloom, H160, H256, U256};
use evm::{
    backend::{ApplyBackend, Backend},
    executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata},
    Config, CreateScheme, ExitReason,
};
use evm_runtime::tracing::using as runtime_using;
use log::{debug, trace};

use crate::{
    backend::EVMBackend,
    blocktemplate::ReceiptAndOptionalContractAddress,
    bytes::Bytes,
    contract::{
        bridge_dfi, bridge_dst20_in, bridge_dst20_out, dst20_allowance, dst20_deploy_contract_tx,
        dst20_deploy_info, dst20_name_info, rename_contract_tx, DST20BridgeInfo,
        DeployContractInfo,
    },
    core::EVMCoreService,
    eventlistener::StorageAccessListener,
    evm::BlockContext,
    fee::{calculate_current_prepay_gas_fee, calculate_gas_fee},
    precompiles::MetachainPrecompiles,
    trace::{listeners, types::single::TraceType, EvmTracer, TracerInput},
    transaction::{
        system::{
            DST20Data, DeployContractData, ExecuteTx, SystemTx, TransferDirection,
            TransferDomainData, UpdateContractNameData,
        },
        SignedTx,
    },
    Result,
};

pub struct AccessListInfo {
    pub access_list: AccessList,
    pub gas_used: U256,
}

#[derive(Debug)]
pub struct ExecutorContext<'a> {
    pub caller: H160,
    pub to: Option<H160>,
    pub value: U256,
    pub data: &'a [u8],
    pub gas_limit: u64,
    pub access_list: AccessList,
}

pub struct AinExecutor<'backend> {
    pub backend: &'backend mut EVMBackend,
}

// State update methods
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

    pub fn update_total_gas_used(&mut self, gas_used: U256) {
        self.backend.update_vicinity_with_gas_used(gas_used)
    }

    pub fn commit(&mut self, is_miner: bool) -> Result<H256> {
        self.backend.commit(is_miner)
    }

    pub fn get_nonce(&self, address: &H160) -> U256 {
        self.backend.get_nonce(address)
    }
}

// EVM executor methods
impl<'backend> AinExecutor<'backend> {
    const CONFIG: Config = Config::shanghai();

    /// Read-only call
    pub fn call(&mut self, ctx: ExecutorContext) -> TxResponse {
        let metadata = StackSubstateMetadata::new(ctx.gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata, self.backend);
        let precompiles = MetachainPrecompiles::default();
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
    pub fn exec(
        &mut self,
        signed_tx: &SignedTx,
        gas_limit: U256,
        base_fee: U256,
        system_tx: bool,
        block_ctx: Option<&BlockContext>,
    ) -> Result<(TxResponse, ReceiptV3)> {
        self.backend.update_vicinity_from_tx(signed_tx)?;
        trace!(
            "[Executor] Executing EVM TX with vicinity : {:?}",
            self.backend.vicinity
        );
        let ctx = ExecutorContext {
            caller: signed_tx.sender,
            to: signed_tx.to(),
            value: signed_tx.value(),
            data: signed_tx.data(),
            gas_limit: u64::try_from(gas_limit).unwrap_or(u64::MAX), // Sets to u64 if overflows
            access_list: signed_tx.access_list(),
        };

        let prepay_fee = if system_tx {
            U256::zero()
        } else {
            calculate_current_prepay_gas_fee(signed_tx, base_fee)?
        };

        if !system_tx {
            self.backend
                .deduct_prepay_gas_fee(signed_tx.sender, prepay_fee)?;
        }

        let metadata = StackSubstateMetadata::new(ctx.gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata, self.backend);
        let precompiles = if let Some(block_ctx) = block_ctx {
            MetachainPrecompiles::new(block_ctx.mnview_ptr)
        } else {
            MetachainPrecompiles::default()
        };
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

        let used_gas = if system_tx { 0u64 } else { executor.used_gas() };
        let total_gas_used = self.backend.vicinity.total_gas_used;
        let block_gas_limit = self.backend.vicinity.block_gas_limit;
        if !system_tx
            && total_gas_used
                .checked_add(U256::from(used_gas))
                .ok_or_else(|| format_err!("total_gas_used overflow"))?
                > block_gas_limit
        {
            return Err(format_err!(
                "[exec] block size limit exceeded, tx cannot make it into the block"
            )
            .into());
        }
        let (values, logs) = executor.into_state().deconstruct();
        let logs = logs.into_iter().collect::<Vec<_>>();

        ApplyBackend::apply(self.backend, values, logs.clone(), true);

        if !system_tx {
            self.backend
                .refund_unused_gas_fee(signed_tx, U256::from(used_gas), base_fee)?;
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

        Ok((
            TxResponse {
                exit_reason,
                data,
                logs,
                used_gas,
            },
            receipt,
        ))
    }

    /// Execute tx with tracer
    pub fn exec_with_tracer(
        &mut self,
        signed_tx: &SignedTx,
        gas_limit: U256,
        system_tx: bool,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<(bool, Vec<u8>, u64)> {
        self.backend.update_vicinity_from_tx(signed_tx)?;
        trace!(
            "[Executor] Executing trace EVM TX with vicinity : {:?}",
            self.backend.vicinity
        );
        // TODO: Buggy, to be removed in tracer revamp fixes.
        let to = signed_tx.to().ok_or(format_err!(
            "debug_traceTransaction does not support contract creation transactions",
        ))?;
        let ctx = ExecutorContext {
            caller: signed_tx.sender,
            to: Some(to),
            value: signed_tx.value(),
            data: signed_tx.data(),
            gas_limit: u64::try_from(gas_limit).unwrap_or(u64::MAX), // Sets to u64 if overflows
            access_list: signed_tx.access_list(),
        };
        let access_list = ctx
            .access_list
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();
        let al = access_list.clone();

        let metadata = StackSubstateMetadata::new(ctx.gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata.clone(), self.backend);
        let precompiles = MetachainPrecompiles::default();
        let mut executor = StackExecutor::new_with_precompiles(state, &Self::CONFIG, &precompiles);

        let f = move || {
            let (exit_reason, data) = executor.transact_call(
                ctx.caller,
                to,
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                al,
            );
            if !exit_reason.is_succeed() {
                debug!("failed VM execution {:?}", exit_reason);
            }
            let used_gas = if system_tx { 0u64 } else { executor.used_gas() };
            (exit_reason.is_succeed(), data, used_gas)
        };

        match tracer_params.1 {
            TraceType::Raw {
                disable_storage,
                disable_memory,
                disable_stack,
            } => {
                let listener = RefCell::new(listeners::Raw::new(
                    disable_storage,
                    disable_memory,
                    disable_stack,
                    raw_max_memory_usage,
                ));
                let tracer = EvmTracer::new(listener);
                Ok(tracer.trace(f))
            }
            TraceType::CallList => {
                let listener = RefCell::new(listeners::CallList::default());
                let tracer = EvmTracer::new(listener);
                Ok(tracer.trace(f))
            }
            not_supported => {
                Err(format_err!("trace_transaction does not support {:?}", not_supported).into())
            }
        }
    }

    /// Execute tx with storage access listener
    pub fn exec_access_list(&self, ctx: ExecutorContext) -> Result<AccessListInfo> {
        let access_list = ctx
            .access_list
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();

        let metadata = StackSubstateMetadata::new(ctx.gas_limit, &Self::CONFIG);
        let state = MemoryStackState::new(metadata.clone(), self.backend);
        let al_state = MemoryStackState::new(metadata, self.backend);
        let precompiles = MetachainPrecompiles::default();
        let mut al_executor =
            StackExecutor::new_with_precompiles(al_state, &Self::CONFIG, &precompiles);
        let mut executor = StackExecutor::new_with_precompiles(state, &Self::CONFIG, &precompiles);
        let mut listener = StorageAccessListener::default();

        let (exit_reason, _) = runtime_using(&mut listener, move || match ctx.to {
            Some(to) => executor.transact_call(
                ctx.caller,
                to,
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
        });
        if !exit_reason.is_succeed() {
            return Err(format_err!("[exec_access_list] tx execution failed").into());
        }

        // Get access list from listener
        let al: AccessList = listener
            .access_list
            .into_iter()
            .map(|(address, storage_keys)| AccessListItem {
                address,
                storage_keys: Vec::from_iter(storage_keys),
            })
            .collect();
        let access_list = al
            .clone()
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();

        // Get gas usage with accumulated access list
        let (exit_reason, _) = match ctx.to {
            Some(to) => al_executor.transact_call(
                ctx.caller,
                to,
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                access_list,
            ),
            None => al_executor.transact_create(
                ctx.caller,
                ctx.value,
                ctx.data.to_vec(),
                ctx.gas_limit,
                access_list,
            ),
        };
        if !exit_reason.is_succeed() {
            return Err(format_err!("[exec_access_list] tx execution failed").into());
        }
        Ok(AccessListInfo {
            access_list: al,
            gas_used: U256::from(al_executor.used_gas()),
        })
    }
}

impl<'backend> AinExecutor<'backend> {
    /// System tx execution
    pub fn execute_tx(
        &mut self,
        tx: ExecuteTx,
        base_fee: U256,
        ctx: Option<&BlockContext>,
    ) -> Result<ApplyTxResult> {
        match tx {
            ExecuteTx::SignedTx(signed_tx) => {
                // Validate nonce
                let nonce = self.backend.get_nonce(&signed_tx.sender);
                if nonce != signed_tx.nonce() {
                    return Err(format_err!(
                        "[execute_tx] nonce check failed. Account nonce {}, signed_tx nonce {}",
                        nonce,
                        signed_tx.nonce(),
                    )
                    .into());
                }

                let (tx_response, receipt) =
                    self.exec(&signed_tx, signed_tx.gas_limit(), base_fee, false, ctx)?;

                debug!(
                    "[execute_tx] receipt : {:?}, exit_reason {:#?} for signed_tx : {:#x}",
                    receipt,
                    tx_response.exit_reason,
                    signed_tx.hash()
                );

                let gas_fee =
                    calculate_gas_fee(&signed_tx, U256::from(tx_response.used_gas), base_fee)?;

                Ok(ApplyTxResult {
                    tx: signed_tx,
                    used_gas: U256::from(tx_response.used_gas),
                    logs: tx_response.logs,
                    gas_fee,
                    receipt: (receipt, None),
                })
            }
            ExecuteTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
                signed_tx,
                direction,
            })) => {
                // Validate nonce
                let nonce = self.backend.get_nonce(&signed_tx.sender);
                if nonce != signed_tx.nonce() {
                    return Err(format_err!(
                        "[execute_tx] nonce check failed. Account nonce {}, signed_tx nonce {}",
                        nonce,
                        signed_tx.nonce(),
                    )
                    .into());
                }

                let to = signed_tx.to().unwrap();
                let input = signed_tx.data();
                let amount = U256::from_big_endian(&input[68..100]);

                debug!(
                    "[execute_tx] Transfer domain: {} from address {:x?}, nonce {:x?}, to address {:x?}, amount: {}",
                    direction, signed_tx.sender, signed_tx.nonce(), to, amount,
                );

                let FixedContract {
                    contract,
                    fixed_address,
                    ..
                } = get_transfer_domain_contract();
                let mismatch = match self.backend.get_account(&fixed_address) {
                    None => false,
                    Some(account) => account.code_hash != contract.codehash,
                };
                if mismatch {
                    debug!(
                        "[execute_tx] {} failed with as transferdomain account codehash mismatch",
                        direction
                    );
                    return Err(format_err!(
                        "[execute_tx] {} failed with as transferdomain account codehash mismatch",
                        direction
                    )
                    .into());
                }

                if direction == TransferDirection::EvmIn {
                    let storage = bridge_dfi(self.backend, amount, direction)?;
                    self.update_storage(fixed_address, storage)?;
                    self.add_balance(fixed_address, amount)?;
                }

                let (tx_response, receipt) =
                    self.exec(&signed_tx, U256::MAX, U256::zero(), true, ctx)?;
                if !tx_response.exit_reason.is_succeed() {
                    return Err(format_err!(
                        "[execute_tx] Transfer domain failed VM execution {:?}",
                        tx_response.exit_reason
                    )
                    .into());
                }

                debug!(
                    "[execute_tx] receipt : {:?}, exit_reason {:#?} for signed_tx : {:#x}, logs: {:x?}",
                    receipt,
                    tx_response.exit_reason,
                    signed_tx.hash(),
                    tx_response.logs
                );

                if direction == TransferDirection::EvmOut {
                    let storage = bridge_dfi(self.backend, amount, direction)?;
                    self.update_storage(fixed_address, storage)?;
                    self.sub_balance(signed_tx.sender, amount)?;
                }

                Ok(ApplyTxResult {
                    tx: signed_tx,
                    used_gas: U256::zero(),
                    logs: tx_response.logs,
                    gas_fee: U256::zero(),
                    receipt: (receipt, None),
                })
            }
            ExecuteTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                signed_tx,
                contract_address,
                direction,
            })) => {
                // Validate nonce
                let nonce = self.backend.get_nonce(&signed_tx.sender);
                if nonce != signed_tx.nonce() {
                    return Err(format_err!(
                        "[execute_tx] nonce check failed. Account nonce {}, signed_tx nonce {}",
                        nonce,
                        signed_tx.nonce(),
                    )
                    .into());
                }

                let input = signed_tx.data();
                let amount = U256::from_big_endian(&input[100..132]);

                debug!(
                    "[execute_tx] DST20Bridge from {}, contract_address {}, amount {}, direction {}",
                    signed_tx.sender, contract_address, amount, direction
                );

                if direction == TransferDirection::EvmIn {
                    let DST20BridgeInfo { address, storage } =
                        bridge_dst20_in(self.backend, contract_address, amount)?;
                    self.update_storage(address, storage)?;
                }

                let allowance = dst20_allowance(direction, signed_tx.sender, amount);
                self.update_storage(contract_address, allowance)?;

                let (tx_response, receipt) =
                    self.exec(&signed_tx, U256::MAX, U256::zero(), true, ctx)?;
                if !tx_response.exit_reason.is_succeed() {
                    debug!(
                        "[execute_tx] DST20 bridge failed VM execution {:?}, data {}",
                        tx_response.exit_reason,
                        hex::encode(&tx_response.data)
                    );
                    return Err(format_err!(
                        "[execute_tx] DST20 bridge failed VM execution {:?}, data {:?}",
                        tx_response.exit_reason,
                        tx_response.data
                    )
                    .into());
                }

                debug!(
                    "[execute_tx] receipt : {:?}, exit_reason {:#?} for signed_tx : {:#x}, logs: {:x?}",
                    receipt,
                    tx_response.exit_reason,
                    signed_tx.hash(),
                    tx_response.logs
                );

                if direction == TransferDirection::EvmOut {
                    let DST20BridgeInfo { address, storage } =
                        bridge_dst20_out(self.backend, contract_address, amount)?;
                    self.update_storage(address, storage)?;
                }

                Ok(ApplyTxResult {
                    tx: signed_tx,
                    used_gas: U256::zero(),
                    logs: tx_response.logs,
                    gas_fee: U256::zero(),
                    receipt: (receipt, None),
                })
            }
            ExecuteTx::SystemTx(SystemTx::DeployContract(DeployContractData {
                name,
                symbol,
                address,
                token_id,
            })) => {
                debug!(
                    "[execute_tx] DeployContract for address {:x?}, name {}, symbol {}",
                    address, name, symbol
                );

                let DeployContractInfo {
                    address,
                    bytecode,
                    storage,
                } = dst20_deploy_info(self.backend, address, &name, &symbol)?;

                self.deploy_contract(address, bytecode, storage)?;
                let (tx, receipt) = dst20_deploy_contract_tx(token_id, &base_fee)?;

                Ok(ApplyTxResult {
                    tx,
                    used_gas: U256::zero(),
                    logs: Vec::new(),
                    gas_fee: U256::zero(),
                    receipt: (receipt, Some(address)),
                })
            }
            ExecuteTx::SystemTx(SystemTx::UpdateContractName(UpdateContractNameData {
                name,
                symbol,
                address,
                token_id,
            })) => {
                debug!(
                    "[execute_tx] Rename contract for address {:x?}, name {}, symbol {}",
                    address, name, symbol
                );

                let storage = dst20_name_info(&name, &symbol);

                self.update_storage(address, storage)?;
                let (tx, receipt) = rename_contract_tx(token_id, &base_fee)?;

                Ok(ApplyTxResult {
                    tx,
                    used_gas: U256::zero(),
                    logs: Vec::new(),
                    gas_fee: U256::zero(),
                    receipt: (receipt, Some(address)),
                })
            }
        }
    }

    /// System tx execution
    pub fn execute_tx_with_tracer(
        &mut self,
        tx: ExecuteTx,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<(bool, Vec<u8>, u64)> {
        // Handle state dependent system txs
        match tx {
            ExecuteTx::SignedTx(signed_tx) => self.exec_with_tracer(
                &signed_tx,
                signed_tx.gas_limit(),
                false,
                tracer_params,
                raw_max_memory_usage,
            ),
            ExecuteTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
                signed_tx,
                direction,
            })) => {
                if direction == TransferDirection::EvmIn {
                    let FixedContract {
                        contract,
                        fixed_address,
                        ..
                    } = get_transfer_domain_contract();
                    let mismatch = match self.backend.get_account(&fixed_address) {
                        None => false,
                        Some(account) => account.code_hash != contract.codehash,
                    };
                    if mismatch {
                        return Err(format_err!("[exec_with_trace] tx trace execution failed as transferdomain account codehash mismatch").into());
                    }
                    let input = signed_tx.data();
                    let amount = U256::from_big_endian(&input[68..100]);
                    let storage = bridge_dfi(self.backend, amount, TransferDirection::EvmIn)?;
                    self.update_storage(fixed_address, storage)?;
                    self.add_balance(fixed_address, amount)?;
                }
                self.exec_with_tracer(
                    &signed_tx,
                    U256::MAX,
                    true,
                    tracer_params,
                    raw_max_memory_usage,
                )
            }
            ExecuteTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                signed_tx,
                contract_address,
                direction,
            })) => {
                if direction == TransferDirection::EvmIn {
                    let input = signed_tx.data();
                    let amount = U256::from_big_endian(&input[100..132]);
                    let DST20BridgeInfo { address, storage } =
                        bridge_dst20_in(self.backend, contract_address, amount)?;
                    self.update_storage(address, storage)?;
                    let allowance =
                        dst20_allowance(TransferDirection::EvmIn, signed_tx.sender, amount);
                    self.update_storage(contract_address, allowance)?;
                }
                self.exec_with_tracer(
                    &signed_tx,
                    U256::MAX,
                    true,
                    tracer_params,
                    raw_max_memory_usage,
                )
            }
            ExecuteTx::SystemTx(SystemTx::DeployContract(_)) => {
                // TODO: running trace on DST20 deployment txs will be buggy at the moment as these custom system txs
                // are never executed on the VM. More thought has to be placed into how we can do accurate traces on
                // the custom system txs related to DST20 tokens, since these txs are state injections on execution.
                // For now, empty execution step with a successful execution trace is returned.
                Ok((true, vec![], 0u64))
            }
            ExecuteTx::SystemTx(SystemTx::UpdateContractName(_)) => {
                // TODO: running trace on DST20 update txs will be buggy at the moment as these custom system txs
                // are never executed on the VM. More thought has to be placed into how we can do accurate traces on
                // the custom system txs related to DST20 tokens, since these txs are state injections on execution.
                // For now, empty execution step with a successful execution trace is returned.
                Ok((true, vec![], 0u64))
            }
        }
    }
}

#[derive(Debug)]
pub struct ApplyTxResult {
    pub tx: Box<SignedTx>,
    pub used_gas: U256,
    pub logs: Vec<Log>,
    pub gas_fee: U256,
    pub receipt: ReceiptAndOptionalContractAddress,
}

#[derive(Debug)]
pub struct TxResponse {
    pub exit_reason: ExitReason,
    pub data: Vec<u8>,
    pub logs: Vec<Log>,
    pub used_gas: u64,
}
