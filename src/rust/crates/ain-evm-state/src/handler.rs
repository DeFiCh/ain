use evm::{
    backend::{MemoryBackend, MemoryVicinity},
    executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata},
    ExitReason,
};
use primitive_types::H160;
use primitive_types::{H256, U256};
use std::collections::BTreeMap;
use std::error::Error;
use std::sync::{Arc, RwLock};

use crate::traits::PersistentState;
use crate::{EVMState, CONFIG, EVM_STATE_PATH, GAS_LIMIT};
use crate::block::BlockHandler;

#[derive(Clone, Debug)]
pub struct EVMHandler {
    pub state: Arc<RwLock<EVMState>>,
}

pub struct Handlers {
    pub evm: EVMHandler,
    pub block: BlockHandler,
}

impl Handlers {
    pub fn new() -> Self {
        Self {
            evm: EVMHandler::new(),
            block: BlockHandler::new(),
        }
    }
}

impl EVMHandler {
    pub fn new() -> Self {
        Self {
            state: Arc::new(RwLock::new(
                EVMState::load_from_disk(EVM_STATE_PATH).unwrap(),
            )),
        }
    }

    pub fn call_evm(
        &self,
        caller: H160,
        address: H160,
        value: U256,
        data: Vec<u8>,
        _gas_limit: u64,
        access_list: Vec<(H160, Vec<H256>)>,
    ) -> (ExitReason, Vec<u8>) {
        // TODO Add actual gas, chain_id, block_number
        let vicinity = MemoryVicinity {
            gas_price: U256::zero(),
            origin: caller,
            block_hashes: Vec::new(),
            block_number: Default::default(),
            block_coinbase: Default::default(),
            block_timestamp: Default::default(),
            block_difficulty: Default::default(),
            block_gas_limit: Default::default(),
            chain_id: U256::one(),
            block_base_fee_per_gas: U256::zero(),
        };
        let state = self.state.read().unwrap().clone();
        let backend = MemoryBackend::new(&vicinity, state);
        let metadata = StackSubstateMetadata::new(GAS_LIMIT, &CONFIG);
        let state = MemoryStackState::new(metadata, &backend);
        let precompiles = BTreeMap::new(); // TODO Add precompile crate
        let mut executor = StackExecutor::new_with_precompiles(state, &CONFIG, &precompiles);
        executor.transact_call(caller, address, value, data, GAS_LIMIT, access_list)
    }

    // TODO wrap in EVM transaction and dryrun with evm_call
    pub fn add_balance(&self, address: &str, value: i64) -> Result<(), Box<dyn Error>> {
        let to = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.entry(to).or_default();
        account.balance = account.balance + value;
        Ok(())
    }

    pub fn sub_balance(&self, address: &str, value: i64) -> Result<(), Box<dyn Error>> {
        let address = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.get_mut(&address).unwrap();
        if account.balance > value.into() {
            account.balance = account.balance - value;
        }
        Ok(())
    }
}
