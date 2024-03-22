use ain_macros::ffi_fallible;
use ain_ocean::Result;
use defichain_rpc::json::blockchain::{Block, Transaction};

use crate::{
    ffi,
    prelude::{cross_boundary_error_return, cross_boundary_success_return},
};

#[ffi_fallible]
pub fn ocean_index_block(block_str: String) -> Result<()> {
    let block: Block<Transaction> = serde_json::from_str(&block_str)?;
    ain_ocean::index_block(&ain_ocean::SERVICES, block)
}

#[ffi_fallible]
pub fn ocean_invalidate_block(block_str: String) -> Result<()> {
    let block: Block<Transaction> = serde_json::from_str(&block_str)?;
    ain_ocean::invalidate_block(block)
}
