pub mod custom_tx;
pub mod types;

use anyhow::format_err;
pub use bitcoin::{
    consensus::{deserialize, serialize},
    Block, Transaction, TxIn, TxOut,
};

pub use crate::types::*;

const OP_PUSHDATA1: u8 = 0x4c;
const OP_PUSHDATA2: u8 = 0x4d;
const OP_PUSHDATA4: u8 = 0x4e;
const OP_RETURN: u8 = 0x6a;
pub const COIN: i64 = 100_000_000;

fn get_dftx_data(data: &[u8]) -> std::result::Result<&[u8], anyhow::Error> {
    if data[0] != OP_RETURN {
        return Err(format_err!("Should start with op return"));
    }
    validate_buffer_len(data[1], data[2..].len())?;

    Ok(&data[2..])
}

fn validate_buffer_len(v: u8, len: usize) -> std::result::Result<(), anyhow::Error> {
    let marker = match len {
        0..=75 => len as u8,
        76..=255 => OP_PUSHDATA1,
        256..=65535 => OP_PUSHDATA2,
        65536..=16777215 => OP_PUSHDATA4,
        _ => return Err(format_err!("OP_PUSHDATA buffer is larger than 16777215")),
    };

    if v != marker {
        return Err(format_err!("OP_PUSHDATA is not between 0x01 or 0x4e"));
    };

    Ok(())
}
