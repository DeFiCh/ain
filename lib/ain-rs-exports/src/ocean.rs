use ain_macros::ffi_fallible;
use ain_ocean::Result;

use crate::{
    ffi,
    prelude::{cross_boundary_error_return, cross_boundary_success_return},
};

#[ffi_fallible]
pub fn ocean_index_block(block: String, block_height: u32) -> Result<()> {
    ain_ocean::index_block(block, block_height)
}

#[ffi_fallible]
pub fn ocean_invalidate_block(block: String, block_height: u32) -> Result<()> {
    ain_ocean::invalidate_block(block, block_height)
}

#[ffi_fallible]
fn ocean_try_set_tx_result(tx_type: u8, tx_hash: [u8; 32], result_ptr: usize) -> Result<()> {
    ain_ocean::tx_result::index(tx_type, tx_hash, result_ptr)
}
