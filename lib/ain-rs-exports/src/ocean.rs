use crate::ffi::{BlockV2Info as BlockV2InfoFFI, CrossBoundaryResult};
use ain_ocean::BlockV2Info;

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
        reward: b.reward.to_owned(),
    }
}

pub fn ocean_index_block(result: &mut CrossBoundaryResult, block: String, b: &BlockV2InfoFFI) {
    match ain_ocean::index_block(block, &convert(b)) {
        Ok(()) => result.ok = true,
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string()
        }
    }
}

pub fn ocean_invalidate_block(result: &mut CrossBoundaryResult, block: String, b: &BlockV2InfoFFI) {
    match ain_ocean::invalidate_block(block, &convert(b)) {
        Ok(()) => result.ok = true,
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string()
        }
    }
}
