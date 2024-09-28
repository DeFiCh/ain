pub mod custom_tx;
mod skipped_tx;
pub mod types;

pub use bitcoin::{
    consensus::{deserialize, serialize},
    Block, Transaction, TxIn, TxOut,
};

pub use crate::types::*;
pub use skipped_tx::is_skipped_tx;

pub const COIN: i64 = 100_000_000;
