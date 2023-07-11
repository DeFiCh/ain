mod backend;
pub mod block;
pub mod bytes;
pub mod core;
mod ecrecover;
pub mod evm;
pub mod executor;
mod fee;
pub mod filters;
mod genesis;
pub mod log;
mod precompiles;
pub mod receipt;
pub mod services;
pub mod storage;
pub mod traits;
pub mod transaction;
mod trie;
mod txqueue;

use thiserror::Error;

pub use anyhow::{Ok, format_err};
pub type Result<T> = std::result::Result<T, EVMError>;

pub type MaybeBlockAny = Option<ethereum::Block<ethereum::TransactionAny>>;
pub type MaybeTransactionV2 = Option<ethereum::TransactionV2>;

#[derive(Error, Debug)]
pub enum EVMError {
    #[error("EVM: Backend trie creation failure: {0:?}")]
    TrieCreationFailed(String),
    #[error("EVM: Backend trie restore failure: {0:?}")]
    TrieRestoreFailed(String),
    #[error("EVM: Insufficient balance error: {0:?}")]
    InsufficientBalance(backend::InsufficientBalance),
    #[error("EVM: Queue no context error")]
    QueueNoSuchContext,
    #[error("EVM: Queue invalid nonce error {0:?}")]
    QueueInvalidNonce((Box<transaction::SignedTx>, ethereum_types::U256)),
    #[error("EVM: Account not found error: {0:?}")]
    NoSuchAccount(ethereum_types::H160),
    #[error("EVM: Trie error: {0:?}")]
    TrieError(String),
    #[error("EVM: IO error")]
    IoError(#[from] std::io::Error),
    #[error("EVM: Bincode error")]
    BincodeError(bincode::Error),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}
