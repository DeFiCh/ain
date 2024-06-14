//! Defichain EVM consensus, runtime and storage implementation

pub mod backend;
pub mod block;
pub mod blocktemplate;
pub mod bytes;
mod contract;
pub mod core;
mod ecrecover;
pub mod evm;
pub mod executor;
pub mod fee;
pub mod filters;
mod gas;
mod genesis;
pub mod log;
mod precompiles;
pub mod receipt;
pub mod services;
pub mod storage;
pub mod subscription;
pub mod trace;
pub mod transaction;
mod trie;
pub mod weiamount;

pub use anyhow::{format_err, Ok};
use backend::BackendError;
use blocktemplate::BlockTemplateError;
use thiserror::Error;
use transaction::TransactionError;
pub type Result<T> = std::result::Result<T, EVMError>;

pub type MaybeBlockAny = Option<ethereum::Block<ethereum::TransactionAny>>;
pub type MaybeTransactionV2 = Option<ethereum::TransactionV2>;

#[derive(Error, Debug)]
pub enum EVMError {
    #[error("EVM: Backend error: {0:?}")]
    TrieCreationFailed(#[from] BackendError),
    #[error("EVM: Block template error {0:?}")]
    BlockTemplateError(#[from] BlockTemplateError),
    #[error("EVM: Exceed money range")]
    MoneyRangeError(String),
    #[error("EVM: IO error")]
    IoError(#[from] std::io::Error),
    #[error("EVM: Hex error")]
    FromHexError(#[from] hex::FromHexError),
    #[error("EVM: Bincode error")]
    BincodeError(#[from] bincode::Error),
    #[error("EVM: Transaction error")]
    TransactionError(#[from] TransactionError),
    #[error("EVM: Storage error")]
    StorageError(String),
    #[error("EVM: serde_json error")]
    JsonError(#[from] serde_json::Error),
    #[error("EVM: serde_json error")]
    JsonRpcError(#[from] jsonrpsee_core::Error),
    #[error("EVM: rocksdb error")]
    RocksDBError(#[from] rocksdb::Error),
    #[error("EVM: db error")]
    DBError(#[from] ain_db::DBError),
    #[error("EVM: ethabi error")]
    EthAbiError(#[from] ethabi::Error),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

impl From<&str> for EVMError {
    fn from(s: &str) -> Self {
        EVMError::Other(format_err!("{s}"))
    }
}
