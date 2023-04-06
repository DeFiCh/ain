use crate::traits::PersistentState;
use ethereum::BlockAny;
use primitive_types::{H256, U256};
use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::io::{Read, Write};

use std::path::Path;
use std::sync::{Arc, RwLock};

pub static BLOCK_MAP_PATH: &str = "block_map.bin";
pub static BLOCK_DATA_PATH: &str = "block_data.bin";

type BlockHashtoBlock = HashMap<H256, U256>;
type Blocks = Vec<BlockAny>;

pub struct BlockHandler {
    pub block_map: Arc<RwLock<BlockHashtoBlock>>,
    pub blocks: Arc<RwLock<Blocks>>,
}

impl PersistentState for BlockHashtoBlock {
    fn save_to_disk(&self, path: &str) -> Result<(), String> {
        let serialized_state = bincode::serialize(self).map_err(|e| e.to_string())?;
        let mut file = File::create(path).map_err(|e| e.to_string())?;
        file.write_all(&serialized_state).map_err(|e| e.to_string())
    }

    fn load_from_disk(path: &str) -> Result<Self, String> {
        if Path::new(path).exists() {
            let mut file = File::open(path).map_err(|e| e.to_string())?;
            let mut data = Vec::new();
            file.read_to_end(&mut data).map_err(|e| e.to_string())?;
            let new_state: HashMap<H256, U256> =
                bincode::deserialize(&data).map_err(|e| e.to_string())?;
            Ok(new_state)
        } else {
            Ok(Self::new())
        }
    }
}

impl PersistentState for Blocks {
    fn save_to_disk(&self, path: &str) -> Result<(), String> {
        let serialized_state = bincode::serialize(self).map_err(|e| e.to_string())?;
        let mut file = File::create(path).map_err(|e| e.to_string())?;
        file.write_all(&serialized_state).map_err(|e| e.to_string())
    }

    fn load_from_disk(path: &str) -> Result<Self, String> {
        if Path::new(path).exists() {
            let mut file = File::open(path).map_err(|e| e.to_string())?;
            let mut data = Vec::new();
            file.read_to_end(&mut data).map_err(|e| e.to_string())?;
            let new_state: Vec<BlockAny> =
                bincode::deserialize(&data).map_err(|e| e.to_string())?;
            Ok(new_state)
        } else {
            Ok(Self::new())
        }
    }
}

impl BlockHandler {
    pub fn new() -> Self {
        Self {
            block_map: Arc::new(RwLock::new(
                BlockHashtoBlock::load_from_disk(BLOCK_MAP_PATH).unwrap(),
            )),
            blocks: Arc::new(RwLock::new(
                Blocks::load_from_disk(BLOCK_DATA_PATH).unwrap(),
            )),
        }
    }

    pub fn connect_block(&self, block: BlockAny) {
        let mut blocks = self.blocks.write().unwrap();
        blocks.push(block.clone());

        let mut blockhash = self.block_map.write().unwrap();
        blockhash.insert(block.header.hash(), block.header.number);
    }

    pub fn flush(&self) {
        self
            .block_map
            .write()
            .unwrap()
            .save_to_disk(BLOCK_MAP_PATH)
            .unwrap();
        self
            .blocks
            .write()
            .unwrap()
            .save_to_disk(BLOCK_DATA_PATH)
            .unwrap();
    }

    pub fn get_block_hash(&self, hash: H256) -> Result<BlockAny, Box<dyn Error>> {
        let block_map = self.block_map.read().unwrap();
        let block_number = *block_map.get(&hash).unwrap();

        let blocks = self.blocks.read().unwrap();
        let block = blocks.get(block_number.as_usize()).unwrap().clone();

        Ok(block)
    }
}
