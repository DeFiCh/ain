pub mod custom_tx;
pub mod types;

pub use bitcoin::{
    consensus::{deserialize, serialize},
    Block, Transaction, TxIn, TxOut,
};

pub use crate::types::*;

pub const COIN: i64 = 100_000_000;
