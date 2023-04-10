pub trait PersistentState {
    fn save_to_disk(&self, path: &str) -> Result<(), PersistentStateError>;
    fn load_from_disk(path: &str) -> Result<Self, PersistentStateError>
    where
        Self: Sized;
}

use std::fmt;
use std::io;

#[derive(Debug)]
pub enum PersistentStateError {
    IoError(io::Error),
    BincodeError(bincode::Error),
}

impl fmt::Display for PersistentStateError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            PersistentStateError::IoError(err) => write!(f, "IO error: {}", err),
            PersistentStateError::BincodeError(err) => write!(f, "Bincode error: {}", err),
        }
    }
}

impl std::error::Error for PersistentStateError {}

impl From<io::Error> for PersistentStateError {
    fn from(error: io::Error) -> Self {
        PersistentStateError::IoError(error)
    }
}

impl From<bincode::Error> for PersistentStateError {
    fn from(error: bincode::Error) -> Self {
        PersistentStateError::BincodeError(error)
    }
}

#[cfg(test)]
mod tests {
    use crate::evm::EVMState;
    use crate::traits::PersistentState;
    use evm::backend::MemoryAccount;
    use primitive_types::{H160, H256, U256};
    use std::collections::BTreeMap;
    use std::fs::File;
    use std::io::Write;

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

        state.save_to_disk(path).unwrap();

        let new_state = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, new_state);
    }

    #[test]
    fn test_invalid_file_format() {
        let invalid_data = b"invalid_data";
        let path = "invalid_file_format.bin";

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
