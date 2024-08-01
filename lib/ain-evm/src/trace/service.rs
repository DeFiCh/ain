use std::{cell::RefCell, num::NonZeroUsize, rc::Rc, sync::Arc};

use anyhow::format_err;
use ethereum::{AccessList, BlockAny};
use ethereum_types::{H160, H256, U256};
use log::debug;
use lru::LruCache;
use parking_lot::Mutex;

use crate::{
    backend::{EVMBackend, Overlay, Vicinity},
    block::INITIAL_BASE_FEE,
    core::EthCallArgs,
    executor::{AinExecutor, ExecutorContext},
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

// The default LRU cache size
const TRACER_TX_LRU_CACHE_DEFAULT_SIZE: usize = 10_000;
const TRACER_BLOCK_LRU_CACHE_DEFAULT_SIZE: usize = 1_000;

pub struct AccessListInfo {
    pub access_list: AccessList,
    pub gas_used: U256,
}
pub struct TraceCache {
    tx_cache: LruCache<(H256, TracerInput), TransactionTrace>,
    block_cache: LruCache<(H256, TracerInput), Vec<(H256, TransactionTrace)>>,
}

pub struct TracerService {
    trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
    tracer_cache: Mutex<TraceCache>,
}

/// Tracer service methods
impl TracerService {
    pub fn new(trie_store: Arc<TrieDBStore>, storage: Arc<Storage>) -> Self {
        Self {
            trie_store,
            storage,
            tracer_cache: Mutex::new(TraceCache {
                tx_cache: LruCache::new(
                    NonZeroUsize::new(TRACER_TX_LRU_CACHE_DEFAULT_SIZE).unwrap(),
                ),
                block_cache: LruCache::new(
                    NonZeroUsize::new(TRACER_BLOCK_LRU_CACHE_DEFAULT_SIZE).unwrap(),
                ),
            }),
        }
    }

    pub fn trace_transaction(
        &self,
        tx: &SignedTx,
        block_number: U256,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<TransactionTrace> {
        if let Some(res) = self.get_tx_trace((tx.hash(), tracer_params.0)) {
            return Ok(res);
        }

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
                let res = self.execute_tx_with_tracer(
                    &mut backend,
                    exec_tx,
                    tracer_params,
                    raw_max_memory_usage,
                    base_fee,
                )?;
                // Add tracer to cache
                self.cache_tx_trace((tx.hash(), tracer_params.0), res.clone());
                return Ok(res);
            }
            AinExecutor::new(&mut backend).execute_tx(exec_tx, base_fee, None)?;
        }
        Err(format_err!("Cannot replay tx, does not exist in block.").into())
    }

    pub fn trace_call(
        &self,
        arguments: EthCallArgs,
        overlay: Option<Overlay>,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<TransactionTrace> {
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
            .get_backend_from_block(Some(block_number), Some(caller), Some(gas_price), overlay)
            .map_err(|e| format_err!("Could not restore backend {}", e))?;
        let ctx = ExecutorContext {
            caller,
            to,
            value,
            data,
            gas_limit,
            access_list,
        };
        self.call_with_tracer(&mut backend, ctx, tracer_params, raw_max_memory_usage)
    }

    pub fn trace_block(
        &self,
        trace_block: BlockAny,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<Vec<(H256, TransactionTrace)>> {
        let block_hash = trace_block.header.hash();
        if let Some(res) = self.get_block_trace((block_hash, tracer_params.0)) {
            return Ok(res);
        }

        // Backend state to start the tx replay should be at the end of the previous block
        let start_block_number = trace_block.header.number.checked_sub(U256::one());
        let mut backend = self
            .get_backend_from_block(start_block_number, None, None, None)
            .map_err(|e| format_err!("Could not restore backend {}", e))?;
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

        let mut res = vec![];
        for (idx, replay_tx) in replay_txs.iter().enumerate() {
            let tx_data = &txs_data[idx];
            let exec_tx = ExecuteTx::from_tx_data(tx_data.clone(), replay_tx.clone())?;
            let trace = if let Some(trace) = self.get_tx_trace((replay_tx.hash(), tracer_params.0))
            {
                AinExecutor::new(&mut backend).execute_tx(exec_tx, base_fee, None)?;
                trace
            } else {
                let trace = self.execute_tx_with_tracer(
                    &mut backend,
                    exec_tx,
                    tracer_params,
                    raw_max_memory_usage,
                    base_fee,
                )?;
                self.cache_tx_trace((replay_tx.hash(), tracer_params.0), trace.clone());
                trace
            };
            res.push((replay_tx.hash(), trace));
        }
        self.cache_block_trace((block_hash, tracer_params.0), res.clone());
        Ok(res)
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
        let ctx = ExecutorContext {
            caller,
            to,
            value,
            data,
            gas_limit,
            access_list,
        };
        let al = self.call_with_access_list_tracer(&mut backend, ctx);

        // Re-execute call to get gas usage
        let mut backend = self
            .get_backend_from_block(Some(block_number), Some(caller), Some(gas_price), None)
            .map_err(|e| format_err!("Could not restore backend {}", e))?;
        let gas_used = AinExecutor::new(&mut backend)
            .call(ExecutorContext {
                caller,
                to,
                value,
                data,
                gas_limit,
                access_list: al.clone(),
            })
            .used_gas;
        Ok(AccessListInfo {
            access_list: al,
            gas_used: U256::from(gas_used),
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

    /// Wraps eth read-only call with tracer
    fn call_with_tracer(
        &self,
        backend: &mut EVMBackend,
        ctx: ExecutorContext,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<TransactionTrace> {
        let f = move || AinExecutor::new(backend).call(ctx);
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
                let tx_res = tracer.trace(f);
                listener
                    .borrow_mut()
                    .finish_transaction(!tx_res.exit_reason.is_succeed(), tx_res.used_gas);
                RawFormatter::format(listener, false).ok_or_else(|| {
                    format_err!(
                        "replayed transaction generated too much data. \
                        try disabling memory or storage?"
                    )
                })?
            }
            TraceType::CallList => {
                let listener = Rc::new(RefCell::new(listeners::CallList::default()));
                let tracer = EvmTracer::new(Rc::clone(&listener));
                tracer.trace(f);
                listener.borrow_mut().finish_transaction();
                match tracer_params.0 {
                    TracerInput::Blockscout => BlockscoutFormatter::format(listener, false),
                    TracerInput::CallTracer => CallTracerFormatter::format(listener, false)
                        .and_then(|mut response| response.pop()),
                    _ => return Err(format_err!("failed to resolve tracer format").into()),
                }
                .ok_or_else(|| format_err!("trace result is empty"))?
            }
        };
        Ok(res)
    }

    /// Wraps eth read-only call with access list tracer
    fn call_with_access_list_tracer(
        &self,
        backend: &mut EVMBackend,
        ctx: ExecutorContext,
    ) -> AccessList {
        let f = move || AinExecutor::new(backend).call(ctx);
        let listener = Rc::new(RefCell::new(listeners::AccessList::default()));
        let tracer = EvmTracer::new(Rc::clone(&listener));
        tracer.trace(f);
        let res = listener.borrow_mut().finish_transaction();
        res
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

    fn get_tx_trace(&self, key: (H256, TracerInput)) -> Option<TransactionTrace> {
        let mut cache = self.tracer_cache.lock();
        cache.tx_cache.get(&key).cloned()
    }

    fn get_block_trace(&self, key: (H256, TracerInput)) -> Option<Vec<(H256, TransactionTrace)>> {
        let mut cache = self.tracer_cache.lock();
        cache.block_cache.get(&key).cloned()
    }

    fn cache_tx_trace(&self, key: (H256, TracerInput), trace_tx: TransactionTrace) {
        let mut cache = self.tracer_cache.lock();
        cache.tx_cache.put(key, trace_tx);
    }

    fn cache_block_trace(
        &self,
        key: (H256, TracerInput),
        block_trace: Vec<(H256, TransactionTrace)>,
    ) {
        let mut cache = self.tracer_cache.lock();
        cache.block_cache.put(key, block_trace);
    }
}
