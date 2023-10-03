mod backend;
pub mod block;
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
pub mod transaction;
mod trie;
pub mod txqueue;
pub mod weiamount;

pub use anyhow::{format_err, Ok};
use backend::BackendError;
use thiserror::Error;
use transaction::TransactionError;
use txqueue::QueueError;
pub type Result<T> = std::result::Result<T, EVMError>;

pub type MaybeBlockAny = Option<ethereum::Block<ethereum::TransactionAny>>;
pub type MaybeTransactionV2 = Option<ethereum::TransactionV2>;

#[derive(Error, Debug)]
pub enum EVMError {
    #[error("EVM: Backend error: {0:?}")]
    TrieCreationFailed(#[from] BackendError),
    #[error("EVM: Queue error {0:?}")]
    QueueError(#[from] QueueError),
    #[error("EVM: Queue invalid nonce error {0:?}")]
    QueueInvalidNonce((Box<transaction::SignedTx>, ethereum_types::U256)),
    #[error("EVM: Exceed block size limit")]
    BlockSizeLimit(String),
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
    #[error("EVM: rocksdb error")]
    RocksDBError(#[from] rocksdb::Error),
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
