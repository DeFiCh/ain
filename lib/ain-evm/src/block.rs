use ethereum::BlockAny;
use keccak_hash::H256;
use primitive_types::U256;
use std::sync::Arc;

use crate::storage::{traits::BlockStorage, Storage};

pub struct BlockHandler {
    storage: Arc<Storage>,
}

impl BlockHandler {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self { storage }
    }

    pub fn get_latest_block_hash_and_number(&self) -> Option<(H256, U256)> {
        self.storage
            .get_latest_block()
            .map(|latest_block| (latest_block.header.hash(), latest_block.header.number))
    }

    pub fn get_latest_state_root(&self) -> H256 {
        self.storage
            .get_latest_block()
            .map(|block| block.header.state_root)
            .unwrap_or_default()
    }

    pub fn connect_block(&self, block: BlockAny) {
        self.storage.put_latest_block(Some(&block));
        self.storage.put_block(&block);
    }
}
