use std::{cell::RefCell, rc::Rc, sync::Arc};

use anyhow::format_err;
use ethereum_types::{H160, U256};
use log::debug;
use parking_lot::Mutex;

use crate::{
    backend::{EVMBackend, Overlay, Vicinity},
    block::INITIAL_BASE_FEE,
    core::EthCallArgs,
    executor::{AccessListInfo, AinExecutor, ExecutorContext},
    storage::{traits::BlockStorage, Storage},
    trace::{
        formatters::{
            Blockscout as BlockscoutFormatter, CallTracer as CallTracerFormatter,
            Raw as RawFormatter, ResponseFormatter,
        },
        get_dst20_system_tx_trace, listeners,
        types::single::{TraceType, TracerInput, TransactionTrace},
        EvmTracer,
    },
    transaction::{
        system::{DeployContractData, ExecuteTx, SystemTx, UpdateContractNameData},
        SignedTx,
    },
    trie::{TrieDBStore, GENESIS_STATE_ROOT},
    Result,
};

// TODO: Add block traces cache
#[derive(Clone, Debug, Default)]
pub struct TracerCache {}

pub struct TracerService {
    pub trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
    pub tracer_cache: Arc<Mutex<TracerCache>>,
}

/// Tracer service methods
impl TracerService {
    pub fn new(trie_store: Arc<TrieDBStore>, storage: Arc<Storage>) -> Self {
        Self {
            trie_store,
            storage,
            tracer_cache: Arc::new(Mutex::new(TracerCache::default())),
        }
    }

    pub fn trace_transaction(
        &self,
        tx: &SignedTx,
        block_number: U256,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<TransactionTrace> {
        // Backend state to start the tx replay should be at the end of the previous block
        let start_block_number = block_number.checked_sub(U256::one());
        let mut backend = self
            .get_backend_from_block(start_block_number, None, None, None)
            .map_err(|e| format_err!("Could not restore backend {}", e))?;
        let trace_block = self
            .storage
            .get_block_by_number(&block_number)?
            .ok_or(format_err!("Block number {:x?} not found", block_number))?;
        backend.update_vicinity_from_header(trace_block.header.clone());
        let base_fee = trace_block.header.base_fee;
        let replay_txs: Vec<_> = trace_block
            .transactions
            .into_iter()
            .flat_map(SignedTx::try_from)
            .collect();
        let txs_data = ain_cpp_imports::get_evm_system_txs_from_block(
            trace_block.header.hash().to_fixed_bytes(),
        );
        if replay_txs.len() != txs_data.len() {
            return Err(format_err!("Cannot replay tx, DVM and EVM block state mismatch.").into());
        }

        for (idx, replay_tx) in replay_txs.iter().enumerate() {
            let tx_data = &txs_data[idx];
            let exec_tx = ExecuteTx::from_tx_data(tx_data.clone(), replay_tx.clone())?;
            if tx.hash() == replay_tx.hash() {
                return self.execute_tx_with_tracer(
                    &mut backend,
                    exec_tx,
                    tracer_params,
                    raw_max_memory_usage,
                    base_fee,
                );
            }
            AinExecutor::new(&mut backend).execute_tx(exec_tx, base_fee, None)?;
        }
        Err(format_err!("Cannot replay tx, does not exist in block.").into())
    }

    pub fn create_access_list(&self, arguments: EthCallArgs) -> Result<AccessListInfo> {
        let EthCallArgs {
            caller,
            to,
            value,
            data,
            gas_limit,
            gas_price,
            access_list,
            block_number,
        } = arguments;
        let mut backend = self
            .get_backend_from_block(Some(block_number), Some(caller), Some(gas_price), None)
            .map_err(|e| format_err!("Could not restore backend {}", e))?;
        AinExecutor::new(&mut backend).exec_access_list(ExecutorContext {
            caller,
            to,
            value,
            data,
            gas_limit,
            access_list,
        })
    }
}

/// Internal tracer service state methods
impl TracerService {
    /// Wraps system tx execution with EVMTracer
    fn execute_tx_with_tracer(
        &self,
        backend: &mut EVMBackend,
        exec_tx: ExecuteTx,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
        base_fee: U256,
    ) -> Result<TransactionTrace> {
        let tx = exec_tx.clone();
        let system_tx = match tx {
            ExecuteTx::SystemTx(_) => true,
            ExecuteTx::SignedTx(_) => false,
        };
        let f = move || AinExecutor::new(backend).execute_tx(exec_tx, base_fee, None);

        let res = match tracer_params.1 {
            TraceType::Raw {
                disable_storage,
                disable_memory,
                disable_stack,
            } => {
                let listener = Rc::new(RefCell::new(listeners::Raw::new(
                    disable_storage,
                    disable_memory,
                    disable_stack,
                    raw_max_memory_usage,
                )));
                let tracer = EvmTracer::new(Rc::clone(&listener));
                let tx_res = tracer.trace(f)?;
                listener.borrow_mut().finish_transaction(
                    !tx_res.exec_flag,
                    u64::try_from(tx_res.used_gas).unwrap_or(u64::MAX),
                );
                RawFormatter::format(listener, system_tx).ok_or_else(|| {
                    format_err!(
                        "replayed transaction generated too much data. \
                        try disabling memory or storage?"
                    )
                })?
            }
            TraceType::CallList => {
                let listener = Rc::new(RefCell::new(listeners::CallList::default()));
                let tracer = EvmTracer::new(Rc::clone(&listener));
                tracer.trace(f)?;
                listener.borrow_mut().finish_transaction();
                match tracer_params.0 {
                    TracerInput::Blockscout => BlockscoutFormatter::format(listener, system_tx),
                    TracerInput::CallTracer => CallTracerFormatter::format(listener, system_tx)
                        .and_then(|mut response| response.pop()),
                    _ => return Err(format_err!("failed to resolve tracer format").into()),
                }
                .ok_or_else(|| format_err!("trace result is empty"))?
            }
        };

        match tx {
            ExecuteTx::SystemTx(SystemTx::DeployContract(DeployContractData {
                address, ..
            })) => get_dst20_system_tx_trace(address, tracer_params),
            ExecuteTx::SystemTx(SystemTx::UpdateContractName(UpdateContractNameData {
                address,
                ..
            })) => get_dst20_system_tx_trace(address, tracer_params),
            _ => Ok(res),
        }
    }

    fn get_backend_from_block(
        &self,
        block_number: Option<U256>,
        caller: Option<H160>,
        gas_price: Option<U256>,
        overlay: Option<Overlay>,
    ) -> Result<EVMBackend> {
        let (state_root, vicinity) = if let Some(block_number) = block_number {
            let block_header = self
                .storage
                .get_block_by_number(&block_number)?
                .map(|block| block.header)
                .ok_or(format_err!("Block number {:x?} not found", block_number))?;
            let state_root = block_header.state_root;
            debug!(
                "Calling EVM at block number : {:#x}, state_root : {:#x}",
                block_number, state_root
            );

            let mut vicinity = Vicinity::from(block_header);
            if let Some(gas_price) = gas_price {
                vicinity.gas_price = gas_price;
            }
            if let Some(caller) = caller {
                vicinity.origin = caller;
            }
            debug!("Vicinity: {:?}", vicinity);
            (state_root, vicinity)
        } else {
            // Handle edge case of no genesis block
            let block_gas_limit =
                U256::from(ain_cpp_imports::get_attribute_values(None).block_gas_limit);
            let vicinity: Vicinity = Vicinity {
                block_number: U256::zero(),
                block_gas_limit,
                block_base_fee_per_gas: INITIAL_BASE_FEE,
                ..Vicinity::default()
            };
            (GENESIS_STATE_ROOT, vicinity)
        };
        EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            vicinity,
            overlay,
        )
    }
}
