use std::sync::Arc;

use ain_contracts::dst20_address_from_token_id;
use ain_cpp_imports::SystemTxType;
use anyhow::format_err;
use ethereum_types::{H160, U256};
use log::debug;
use parking_lot::Mutex;

use crate::{
    backend::{EVMBackend, Overlay, Vicinity},
    block::INITIAL_BASE_FEE,
    core::EthCallArgs,
    eventlistener::ExecutionStep,
    executor::{AccessListInfo, AinExecutor, ExecuteTx, ExecutorContext},
    storage::{traits::BlockStorage, Storage},
    transaction::{
        system::{DST20Data, SystemTx, TransferDirection, TransferDomainData},
        SignedTx,
    },
    trie::{TrieDBStore, GENESIS_STATE_ROOT},
    Result,
};

// TODO: Add tracer result caching
#[derive(Clone, Debug, Default)]
pub struct TracerCache {}

pub struct TracerService {
    pub trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
    pub tracer_cache: Arc<Mutex<TracerCache>>,
}

impl TracerService {
    pub fn new(trie_store: Arc<TrieDBStore>, storage: Arc<Storage>) -> Self {
        Self {
            trie_store,
            storage,
            tracer_cache: Arc::new(Mutex::new(TracerCache::default())),
        }
    }

    pub fn get_backend_from_block(
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

    pub fn call_with_tracer(
        &self,
        tx: &SignedTx,
        block_number: U256,
    ) -> Result<(Vec<ExecutionStep>, bool, Vec<u8>, u64)> {
        // Backend state to start the tx replay should be at the end of the previous block
        let start_block_number = block_number.checked_sub(U256::one());
        let mut backend = self
            .get_backend_from_block(start_block_number, None, None, None)
            .map_err(|e| format_err!("Could not restore backend {}", e))?;
        let block = self
            .storage
            .get_block_by_number(&block_number)?
            .ok_or(format_err!("Block number {:x?} not found", block_number))?;
        backend.update_vicinity_from_header(block.header.clone());
        let base_fee = block.header.base_fee;
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

        for (idx, replay_tx) in replay_txs.iter().enumerate() {
            let tx_data = &txs_data[idx];

            if tx.hash() == replay_tx.hash() {
                backend.update_vicinity_from_tx(tx)?;
                // TODO: Pass tx type to tracer and add execute system tx with tracer pipeline
                return AinExecutor::new(&mut backend).exec_with_tracer(tx);
            }

            let exec_tx = match tx_data.tx_type {
                SystemTxType::EVMTx => ExecuteTx::SignedTx(Box::new(replay_tx.clone())),
                SystemTxType::TransferDomainIn => {
                    ExecuteTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
                        signed_tx: Box::new(replay_tx.clone()),
                        direction: TransferDirection::EvmIn,
                    }))
                }
                SystemTxType::TransferDomainOut => {
                    ExecuteTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
                        signed_tx: Box::new(replay_tx.clone()),
                        direction: TransferDirection::EvmOut,
                    }))
                }
                SystemTxType::DST20BridgeIn => {
                    let contract_address = dst20_address_from_token_id(tx_data.token_id)?;
                    ExecuteTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                        signed_tx: Box::new(replay_tx.clone()),
                        contract_address,
                        direction: TransferDirection::EvmIn,
                    }))
                }
                SystemTxType::DST20BridgeOut => {
                    let contract_address = dst20_address_from_token_id(tx_data.token_id)?;
                    ExecuteTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                        signed_tx: Box::new(replay_tx.clone()),
                        contract_address,
                        direction: TransferDirection::EvmOut,
                    }))
                }
                _ => return Err(format_err!("Cannot replay tx, system tx type error.").into()),
            };
            AinExecutor::new(&mut backend).execute_tx(exec_tx, base_fee)?;
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
