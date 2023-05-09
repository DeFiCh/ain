use crate::receipt::Receipt;
use ethereum::BlockAny;
use ethereum::TransactionV2;
use keccak_hash::H256;
use log::debug;
use primitive_types::U256;
use std::fs::File;

use std::fmt;
use std::io;
use std::io::Write;
use std::path::Path;
use std::path::PathBuf;

pub trait BlockStorage {
    fn get_block_by_number(&self, number: &U256) -> Option<BlockAny>;
    fn get_block_by_hash(&self, block_hash: &H256) -> Option<BlockAny>;
    fn put_block(&self, block: &BlockAny);
    fn get_latest_block(&self) -> Option<BlockAny>;
    fn put_latest_block(&self, block: &BlockAny);
}

pub trait TransactionStorage {
    fn extend_transactions_from_block(&self, block: &BlockAny);
    fn get_transaction_by_hash(&self, hash: &H256) -> Option<TransactionV2>;
    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: &H256,
        index: usize,
    ) -> Option<TransactionV2>;
    fn get_transaction_by_block_number_and_index(
        &self,
        number: &U256,
        index: usize,
    ) -> Option<TransactionV2>;
    fn put_transaction(&self, transaction: &TransactionV2);
}

pub trait ReceiptStorage {
    fn get_receipt(&self, tx: &H256) -> Option<Receipt>;
    fn put_receipts(&self, receipts: Vec<Receipt>);
}

pub trait FlushableStorage {
    fn flush(&self) -> Result<(), PersistentStateError>;
}

pub trait PersistentState {
    fn save_to_disk(&self, file_path: &str) -> Result<(), PersistentStateError>
    where
        Self: serde::ser::Serialize,
    {
        // Automatically resolves from datadir for now
        let path = match ain_cpp_imports::get_datadir() {
            Ok(path) => {
                let path = PathBuf::from(path).join("evm");
                if !path.exists() {
                    std::fs::create_dir(&path).expect("Error creating `evm` dir");
                }
                path.join(file_path)
            }
            _ => PathBuf::from(file_path),
        };

        let serialized_state = bincode::serialize(self)?;
        let mut file = File::create(path)?;
        file.write_all(&serialized_state)?;
        Ok(())
    }

    fn load_from_disk(file_path: &str) -> Result<Self, PersistentStateError>
    where
        Self: Sized + serde::de::DeserializeOwned + Default,
    {
        debug!("Restoring {} from disk", file_path);

        // Automatically resolves from datadir for now
        let path = match ain_cpp_imports::get_datadir() {
            Ok(path) => PathBuf::from(path).join("evm").join(file_path),
            _ => PathBuf::from(file_path),
        };

        if Path::new(&path).exists() {
            let file = File::open(path)?;
            let new_state: Self = bincode::deserialize_from(file)?;
            Ok(new_state)
        } else {
            Ok(Self::default())
        }
    }
}

#[derive(Debug)]
pub enum PersistentStateError {
    IoError(io::Error),
    BincodeError(bincode::Error),
}

impl fmt::Display for PersistentStateError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            PersistentStateError::IoError(err) => write!(f, "IO error: {err}"),
            PersistentStateError::BincodeError(err) => write!(f, "Bincode error: {err}"),
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
