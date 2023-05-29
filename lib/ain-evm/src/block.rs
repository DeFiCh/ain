use ethereum::BlockAny;
use keccak_hash::H256;
use primitive_types::U256;
use std::cmp::max;
use std::sync::Arc;

use crate::storage::{traits::BlockStorage, Storage};

pub struct BlockHandler {
    storage: Arc<Storage>,
}

impl BlockHandler {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self { storage }
    }

    pub fn get_latest_block_hash_and_number(&self) -> (H256, U256) {
        self.storage
            .get_latest_block()
            .map_or((H256::default(), U256::zero()), |latest_block| {
                (latest_block.header.hash(), latest_block.header.number)
            })
    }

    pub fn get_latest_state_root(&self) -> H256 {
        self.storage
            .get_latest_block()
            .map(|block| block.header.state_root)
            .unwrap_or_default()
    }

    pub fn connect_block(&self, block: BlockAny, base_fee: U256) {
        self.storage.put_latest_block(Some(&block));
        self.storage.put_block(&block);
        self.storage.set_base_fee(block.header.hash(), base_fee);
    }

    pub fn calculate_base_fee(&self, parent_hash: H256) -> U256 {
        // constants
        let initial_base_fee: U256 = U256::from(1_000_000_000); // wei
        let base_fee_max_change_denominator: U256 = U256::from(8);
        let elasticity_multiplier: U256 = U256::from(2);

        // first block has 1 gwei base fee
        if parent_hash == H256::zero() {
            return initial_base_fee;
        }

        // get parent gas usage,
        // https://eips.ethereum.org/EIPS/eip-1559#:~:text=fee%20is%20correct-,if%20INITIAL_FORK_BLOCK_NUMBER%20%3D%3D%20block.number%3A,-expected_base_fee_per_gas%20%3D%20INITIAL_BASE_FEE
        let parent_block = self
            .storage
            .get_block_by_hash(&parent_hash)
            .expect("Parent block not found");
        let parent_base_fee = self
            .storage
            .get_base_fee(&parent_block.header.hash())
            .expect("Parent base fee not found");
        let parent_gas_used = parent_block.header.gas_used;
        let parent_gas_target = parent_block.header.gas_limit / elasticity_multiplier;

        return if parent_gas_used == parent_gas_target {
            parent_base_fee
        } else if parent_gas_used > parent_gas_target {
            let gas_used_delta = parent_gas_used - parent_gas_target;
            let base_fee_per_gas_delta = max(
                parent_base_fee * gas_used_delta
                    / parent_gas_target
                    / base_fee_max_change_denominator,
                U256::from(1),
            );

            parent_base_fee + base_fee_per_gas_delta
        } else {
            let gas_used_delta = parent_gas_target - parent_gas_used;
            let base_fee_per_gas_delta = parent_base_fee * gas_used_delta
                / parent_gas_target
                / base_fee_max_change_denominator;

            parent_base_fee - base_fee_per_gas_delta
        };
    }
}
