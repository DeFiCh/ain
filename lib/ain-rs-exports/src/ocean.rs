use crate::ffi::CrossBoundaryResult;

pub fn ocean_index_block(_result: &mut CrossBoundaryResult, block: String, block_height: u32) {
    ain_ocean::index_block(block, block_height);
}

pub fn ocean_invalidate_block(_result: &mut CrossBoundaryResult, block: String, block_height: u32) {
    ain_ocean::invalidate_block(block, block_height);
}
