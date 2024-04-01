use std::{num::NonZeroUsize, sync::Arc};

use anyhow::format_err;
use ethereum::BlockAny;
use ethereum_types::{H160, H256, U256};
use log::debug;
use lru::LruCache;
use parking_lot::Mutex;

use crate::{
    backend::{EVMBackend, Overlay, Vicinity},
    block::INITIAL_BASE_FEE,
    core::EthCallArgs,
    executor::{AccessListInfo, AinExecutor, ExecutorContext},
    storage::{traits::BlockStorage, Storage},
    trace::types::single::{TraceType, TracerInput, TransactionTrace},
    transaction::{system::ExecuteTx, SignedTx},
    trie::{TrieDBStore, GENESIS_STATE_ROOT},
    Result,
};

// The default LRU cache size
const TRACER_LRU_CACHE_DEFAULT_SIZE: usize = 10_000;

pub struct TraceCache {
    tx_cache: LruCache<H256, TransactionTrace>,
    block_cache: LruCache<H256, Vec<TransactionTrace>>,
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
                tx_cache: LruCache::new(NonZeroUsize::new(TRACER_LRU_CACHE_DEFAULT_SIZE).unwrap()),
                block_cache: LruCache::new(
                    NonZeroUsize::new(TRACER_LRU_CACHE_DEFAULT_SIZE).unwrap(),
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
        if let Some(res) = self.get_tx_trace(tx.hash()) {
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
                let res = AinExecutor::new(&mut backend).execute_tx_with_tracer(
                    exec_tx,
                    tracer_params,
                    raw_max_memory_usage,
                    base_fee,
                )?;
                // Add tracer to cache
                self.cache_tx_trace(tx.hash(), res.clone());
                return Ok(res);
            }
            AinExecutor::new(&mut backend).execute_tx(exec_tx, base_fee, None)?;
        }
        Err(format_err!("Cannot replay tx, does not exist in block.").into())
    }

    pub fn trace_block(
        &self,
        block: BlockAny,
        tracer_params: (TracerInput, TraceType),
        raw_max_memory_usage: usize,
    ) -> Result<Vec<TransactionTrace>> {
        let block_hash = block.header.hash();
        if let Some(res) = self.get_block_trace(block_hash) {
            return Ok(res);
        }

        let base_fee = block.header.base_fee;
        let state_root = block.header.state_root;
        let vicinity = Vicinity::from(block.header.clone());
        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            vicinity,
            None,
        )?;
        backend.update_vicinity_from_header(block.header.clone());
        let replay_txs: Vec<_> = block
            .transactions
            .into_iter()
            .flat_map(SignedTx::try_from)
            .collect();
        let txs_data =
            ain_cpp_imports::get_evm_system_txs_from_block(block.header.hash().to_fixed_bytes());
        if replay_txs.len() != txs_data.len() {
            return Err(format_err!("Cannot replay tx, DVM and EVM block state mismatch.").into());
        }

        let mut res = vec![];
        for (idx, replay_tx) in replay_txs.iter().enumerate() {
            let tx_data = &txs_data[idx];
            let exec_tx = ExecuteTx::from_tx_data(tx_data.clone(), replay_tx.clone())?;
            let trace = if let Some(trace) = self.get_tx_trace(replay_tx.hash()) {
                AinExecutor::new(&mut backend).execute_tx(exec_tx, base_fee, None)?;
                trace
            } else {
                let trace = AinExecutor::new(&mut backend).execute_tx_with_tracer(
                    exec_tx,
                    tracer_params,
                    raw_max_memory_usage,
                    base_fee,
                )?;
                self.cache_tx_trace(replay_tx.hash(), trace.clone());
                trace
            };
            res.push(trace);
        }
        self.cache_block_trace(block_hash, res.clone());
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

    fn get_tx_trace(&self, tx_hash: H256) -> Option<TransactionTrace> {
        let mut cache = self.tracer_cache.lock();
        cache.tx_cache.get(&tx_hash).cloned()
    }

    fn get_block_trace(&self, block_hash: H256) -> Option<Vec<TransactionTrace>> {
        let mut cache = self.tracer_cache.lock();
        cache.block_cache.get(&block_hash).cloned()
    }

    fn cache_tx_trace(&self, tx_hash: H256, trace_tx: TransactionTrace) {
        let mut cache = self.tracer_cache.lock();
        cache.tx_cache.put(tx_hash, trace_tx);
    }

    fn cache_block_trace(&self, block_hash: H256, block_trace: Vec<TransactionTrace>) {
        let mut cache = self.tracer_cache.lock();
        cache.block_cache.put(block_hash, block_trace);
    }
}
