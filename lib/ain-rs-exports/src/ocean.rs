use crate::ffi::CrossBoundaryResult;

pub fn ocean_index_block(result: &mut CrossBoundaryResult, block: String, block_height: u32) {
    match ain_ocean::index_block(block, block_height) {
        Ok(()) => result.ok = true,
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string()
        }
    }
}

pub fn ocean_invalidate_block(result: &mut CrossBoundaryResult, block: String, block_height: u32) {
    match ain_ocean::invalidate_block(block, block_height) {
        Ok(()) => result.ok = true,
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string()
        }
    }
}
