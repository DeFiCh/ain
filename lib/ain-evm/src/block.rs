use ethereum::{Block, BlockAny, PartialHeader};
use keccak_hash::H256;
use primitive_types::U256;
use std::{fs, io::BufReader, path::PathBuf, sync::Arc};

use crate::{
    genesis::GenesisData,
    storage::{traits::BlockStorage, Storage},
};

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

    pub fn connect_block(&self, block: BlockAny) {
        self.storage.put_latest_block(Some(&block));
        self.storage.put_block(&block);
    }
}

pub fn new_block_from_json(path: PathBuf, state_root: H256) -> Result<BlockAny, std::io::Error> {
    let file = fs::File::open(path)?;
    let reader = BufReader::new(file);
    let genesis: GenesisData = serde_json::from_reader(reader)?;

    Ok(Block::new(
        PartialHeader {
            state_root,
            beneficiary: genesis.coinbase,
            timestamp: genesis.timestamp,
            difficulty: genesis.difficulty,
            extra_data: genesis.extra_data,
            number: U256::zero(),
            parent_hash: Default::default(),
            receipts_root: Default::default(),
            logs_bloom: Default::default(),
            gas_limit: Default::default(),
            gas_used: Default::default(),
            mix_hash: Default::default(),
            nonce: Default::default(),
        },
        Vec::new(),
        Vec::new(),
    ))
}
