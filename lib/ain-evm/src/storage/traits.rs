use std::{
    collections::HashMap,
    fs::File,
    io::Write,
    path::{Path, PathBuf},
};

use ethereum::{BlockAny, TransactionV2};
use ethereum_types::{H160, U256};
use keccak_hash::H256;
use log::debug;

use crate::{log::LogIndex, receipt::Receipt, Result};

pub trait BlockStorage {
    fn get_block_by_number(&self, number: &U256) -> Result<Option<BlockAny>>;
    fn get_block_by_hash(&self, block_hash: &H256) -> Result<Option<BlockAny>>;
    fn put_block(&self, block: &BlockAny) -> Result<()>;
    fn get_latest_block(&self) -> Result<Option<BlockAny>>;
    fn put_latest_block(&self, block: Option<&BlockAny>) -> Result<()>;
}

pub trait TransactionStorage {
    fn put_transactions_from_block(&self, block: &BlockAny) -> Result<()>;
    fn get_transaction_by_hash(&self, hash: &H256) -> Result<Option<TransactionV2>>;
    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: &H256,
        index: usize,
    ) -> Result<Option<TransactionV2>>;
    fn get_transaction_by_block_number_and_index(
        &self,
        number: &U256,
        index: usize,
    ) -> Result<Option<TransactionV2>>;
}

pub trait ReceiptStorage {
    fn get_receipt(&self, tx: &H256) -> Result<Option<Receipt>>;
    fn put_receipts(&self, receipts: Vec<Receipt>) -> Result<()>;
}

pub trait LogStorage {
    fn get_logs(&self, block_number: &U256) -> Result<Option<HashMap<H160, Vec<LogIndex>>>>;
    fn put_logs(&self, address: H160, logs: Vec<LogIndex>, block_number: U256) -> Result<()>;
}

pub trait FlushableStorage {
    fn flush(&self) -> Result<()>;
}

pub trait Rollback {
    fn disconnect_latest_block(&self) -> Result<()>;
}

pub trait PersistentState {
    fn save_to_disk(&self, file_path: &str) -> Result<()>
    where
        Self: serde::ser::Serialize,
    {
        // Automatically resolves from datadir for now
        let datadir = ain_cpp_imports::get_datadir();
        let path = {
            let path = PathBuf::from(datadir).join("evm");
            if !path.exists() {
                std::fs::create_dir(&path).expect("Error creating `evm` dir");
            }
            path.join(file_path)
        };

        let serialized_state = bincode::serialize(self)?;
        let mut file = File::create(path)?;
        file.write_all(&serialized_state)?;
        Ok(())
    }

    fn load_from_disk(file_path: &str) -> Result<Self>
    where
        Self: Sized + serde::de::DeserializeOwned + Default,
    {
        debug!("Restoring {} from disk", file_path);

        // Automatically resolves from datadir for now
        let datadir = ain_cpp_imports::get_datadir();
        let path = PathBuf::from(datadir).join("evm").join(file_path);

        if Path::new(&path).exists() {
            let file = File::open(path)?;
            let new_state: Self = bincode::deserialize_from(file)?;
            Ok(new_state)
        } else {
            Ok(Self::default())
        }
    }
}
