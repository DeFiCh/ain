use ain_macros::ffi_fallible;
use ain_ocean::{PoolCreationHeight, Result};
use defichain_rpc::json::blockchain::{Block, Transaction};

use crate::{
    ffi,
    prelude::{cross_boundary_error_return, cross_boundary_success_return},
};

#[ffi_fallible]
pub fn ocean_index_block(block_str: String, pools: Vec<ffi::PoolCreationHeight>) -> Result<()> {
    let block: Block<Transaction> = serde_json::from_str(&block_str)?;
    let pools = pools
        .into_iter()
        .map(|p| PoolCreationHeight {
            id: p.id,
            id_token_a: p.id_token_a,
            id_token_b: p.id_token_b,
            creation_height: p.creation_height,
        })
        .collect::<Vec<_>>();
    ain_ocean::index_block(&ain_ocean::SERVICES, block, pools)
}

#[ffi_fallible]
pub fn ocean_invalidate_block(block_str: String) -> Result<()> {
    let block: Block<Transaction> = serde_json::from_str(&block_str)?;
    ain_ocean::invalidate_block(block)
}

#[ffi_fallible]
fn ocean_try_set_tx_result(tx_type: u8, tx_hash: [u8; 32], result_ptr: usize) -> Result<()> {
    ain_ocean::tx_result::index(&ain_ocean::SERVICES, tx_type, tx_hash, result_ptr)
}
