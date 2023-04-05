use std::collections::BTreeMap;
use std::error::Error;
use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;
use std::sync::{Arc, RwLock};
use evm::backend::{MemoryAccount, MemoryBackend, MemoryVicinity};
use evm::{Config, ExitReason};
use evm::executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata};
use primitive_types::{H160, H256, U256};
use crate::traits::PersistentState;

pub static CONFIG: Config = Config::london();
pub static GAS_LIMIT: u64 = u64::MAX;
pub static EVM_STATE_PATH: &str = "evm_state.bin";

pub type EVMState = BTreeMap<H160, MemoryAccount>;

#[derive(Clone, Debug)]
pub struct EVMHandler {
    pub state: Arc<RwLock<EVMState>>,
}

impl PersistentState for EVMState {
    fn save_to_disk(&self, path: &str) -> Result<(), String> {
        let serialized_state = bincode::serialize(self).map_err(|e| e.to_string())?;
        let mut file = File::create(path).map_err(|e| e.to_string())?;
        file.write_all(&serialized_state).map_err(|e| e.to_string())
    }

    fn load_from_disk(path: &str) -> Result<Self, String> {
        if Path::new(path).exists() {
            let mut file = File::open(path).map_err(|e| e.to_string())?;
            let mut data = Vec::new();
            file.read_to_end(&mut data).map_err(|e| e.to_string())?;
            let new_state: BTreeMap<H160, MemoryAccount> =
                bincode::deserialize(&data).map_err(|e| e.to_string())?;
            Ok(new_state)
        } else {
            Ok(Self::new())
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


#[cfg(test)]
mod tests {
    use super::*;
    use primitive_types::{H256, U256};

    fn create_account(
        nonce: U256,
        balance: U256,
        code: Vec<u8>,
        storage: BTreeMap<H256, H256>,
    ) -> MemoryAccount {
        MemoryAccount {
            nonce,
            balance,
            code,
            storage,
        }
    }

    #[test]
    fn test_load_non_existent_file() {
        let state = EVMState::load_from_disk("non_existent_file.bin");
        assert!(state.is_err());
    }

    #[test]
    fn test_empty_file() {
        let state = BTreeMap::new();
        let path = "empty_test.bin";

        // Save to an empty file
        state.save_to_disk(path).unwrap();

        let new_state = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, new_state);
    }

    #[test]
    fn test_invalid_file_format() {
        let invalid_data = b"invalid_data";
        let path = "invalid_file_format.bin";

        // Write invalid data to a file
        let mut file = File::create(path).unwrap();
        file.write_all(invalid_data).unwrap();

        let state = EVMState::load_from_disk(path);

        assert!(state.is_err());
    }

    #[test]
    fn test_save_and_load_empty_backend() {
        let path = "test_empty_backend.bin";
        let state = BTreeMap::new();

        state.save_to_disk(path).unwrap();

        let loaded_backend = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, loaded_backend);
    }

    #[test]
    fn test_save_and_load_single_account() {
        let path = "test_single_account.bin";
        let mut state = BTreeMap::new();

        let account = create_account(
            U256::from(1),
            U256::from(1000),
            vec![1, 2, 3],
            BTreeMap::new(),
        );
        let address = H160::from_low_u64_be(1);
        state.insert(address, account);

        state.save_to_disk(path).unwrap();

        let loaded_backend = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, loaded_backend);
    }

    #[test]
    fn test_save_and_load_multiple_accounts() {
        let path = "test_multiple_accounts.bin";
        let mut state = BTreeMap::new();

        let account1 = create_account(
            U256::from(1),
            U256::from(1000),
            vec![1, 2, 3],
            BTreeMap::new(),
        );
        let address1 = H160::from_low_u64_be(1);
        state.insert(address1, account1);

        let account2 = create_account(
            U256::from(2),
            U256::from(2000),
            vec![4, 5, 6],
            BTreeMap::new(),
        );
        let address2 = H160::from_low_u64_be(2);
        state.insert(address2, account2);

        state.save_to_disk(path).unwrap();

        let loaded_backend = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, loaded_backend);
    }
}
