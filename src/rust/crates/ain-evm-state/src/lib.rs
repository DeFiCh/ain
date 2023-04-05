pub mod handler;
pub mod traits;
pub mod tx_queue;
use std::collections::BTreeMap;

use crate::traits::PersistentState;

pub use evm::backend::Backend;
use evm::backend::MemoryAccount;
use primitive_types::H160;
use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;

pub static GAS_LIMIT: u64 = u64::MAX;
pub static EVM_STATE_PATH: &str = "evm_state.bin";

pub type EVMState = BTreeMap<H160, MemoryAccount>;

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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::handler::{EVMHandler, ExitReason, ExitSucceed};
    use ethereum::AccessListItem;
    use primitive_types::{H160, H256, U256};
    use std::str::FromStr;

    const ALICE: &str = "0x0000000000000000000000000000000000000000";
    const BOB: &str = "0x0000000000000000000000000000000000000001";

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
        let state = EVMState::load_from_disk("non_existent_file.bin").unwrap();
        assert_eq!(state, EVMState::default());
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

    #[test]
    fn should_call_evm() {
        let handler = EVMHandler::new();
        let _ = handler.add_balance(ALICE, U256::from(1000));
        let item = AccessListItem {
            address: H160::default(),
            storage_keys: vec![H256::default()],
        };

        let res = handler.call(
            Some(H160::from_str(ALICE).unwrap()),
            Some(H160::from_str(BOB).unwrap()),
            U256::from(1000u64),
            &vec![u8::default()],
            100000u64,
            vec![item],
        );
        assert_eq!(res, (ExitReason::Succeed(ExitSucceed::Stopped), vec![]))
    }
}
