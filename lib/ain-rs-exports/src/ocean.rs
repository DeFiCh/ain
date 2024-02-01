use std::sync::Arc;

use ain_macros::ffi_fallible;
use ain_ocean::Result;
use defichain_rpc::json::blockchain::{Block, Transaction};

use crate::{
    ffi,
    prelude::{cross_boundary_error_return, cross_boundary_success_return},
};

#[ffi_fallible]
pub fn ocean_index_block(block_str: String) -> Result<()> {
    let services = &*ain_ocean::SERVICES;
    let block: Block<Transaction> = serde_json::from_str(&block_str)?;
    ain_ocean::index_block(Arc::clone(services), block)
}

#[ffi_fallible]
pub fn ocean_invalidate_block(block_str: String) -> Result<()> {
    let block: Block<Transaction> = serde_json::from_str(&block_str)?;
    ain_ocean::invalidate_block(block)
}

#[ffi_fallible]
fn ocean_try_set_tx_result(tx_type: u8, tx_hash: [u8; 32], result_ptr: usize) -> Result<()> {
    let services = &*ain_ocean::SERVICES;
    ain_ocean::tx_result::index(Arc::clone(services), tx_type, tx_hash, result_ptr)
}
