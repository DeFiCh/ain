use ain_macros::ffi_fallible;
use ain_ocean::{BlockV2Info, Result};

use crate::{
    ffi,
    ffi::BlockV2Info as BlockV2InfoFFI,
    prelude::{cross_boundary_error_return, cross_boundary_success_return},
};

// manually convert since BlockV2InfoFFI is belongs to CPP which can't impl From -> Into
pub fn convert(b: &BlockV2InfoFFI) -> BlockV2Info {
    BlockV2Info {
        height: b.height,
        difficulty: b.difficulty,
        version: b.version,
        median_time: b.median_time,
        minter_block_count: b.minter_block_count,
        size: b.size,
        size_stripped: b.size_stripped,
        weight: b.weight,
        stake_modifier: b.stake_modifier.to_owned(),
        minter: b.minter.to_owned(),
        masternode: b.masternode.to_owned(),
    }
}

#[ffi_fallible]
pub fn ocean_index_block(block: String, b: &BlockV2InfoFFI) -> Result<()> {
    ain_ocean::index_block(block, &convert(b))
}

#[ffi_fallible]
pub fn ocean_invalidate_block(block: String, b: &BlockV2InfoFFI) -> Result<()> {
    ain_ocean::invalidate_block(block, &convert(b))
}

#[ffi_fallible]
fn ocean_try_set_tx_result(tx_type: u8, tx_hash: [u8; 32], result_ptr: usize) -> Result<()> {
    ain_ocean::tx_result::index(tx_type, tx_hash, result_ptr)
}

#[ffi_fallible]
pub fn ocean_invalidate_transaction(tx_hash: [u8; 32]) -> Result<()> {
    ain_ocean::invalidate_transaction(tx_hash)
}
#[ffi_fallible]
pub fn ocean_invalidate_transaction_vin(tx_hash: String) -> Result<()> {
    ain_ocean::invalidate_transaction_vin(tx_hash)
}
#[ffi_fallible]
pub fn ocean_invalidate_transaction_vout(tx_hash: String) -> Result<()> {
    ain_ocean::invalidate_transaction_vout(tx_hash)
}
