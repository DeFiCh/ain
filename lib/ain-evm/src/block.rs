use crate::traits::{PersistentState, PersistentStateError};
use ethereum::BlockAny;
use primitive_types::H256;
use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::io::{Read, Write};

use std::path::Path;
use std::sync::{Arc, RwLock};

pub static BLOCK_MAP_PATH: &str = "block_map.bin";
pub static BLOCK_DATA_PATH: &str = "block_data.bin";

type BlockHashtoBlock = HashMap<H256, usize>;
type Blocks = Vec<BlockAny>;

pub struct BlockHandler {
    pub block_map: Arc<RwLock<BlockHashtoBlock>>,
    pub blocks: Arc<RwLock<Blocks>>,
}

impl PersistentState for BlockHashtoBlock {
    fn save_to_disk(&self, path: &str) -> Result<(), PersistentStateError> {
        let serialized_state = bincode::serialize(self)?;
        let mut file = File::create(path)?;
        file.write_all(&serialized_state)?;
        Ok(())
    }

    fn load_from_disk(path: &str) -> Result<Self, PersistentStateError> {
        if Path::new(path).exists() {
            let mut file = File::open(path)?;
            let mut data = Vec::new();
            file.read_to_end(&mut data)?;
            let new_state: HashMap<H256, usize> = bincode::deserialize(&data)?;
            Ok(new_state)
        } else {
            Ok(Self::new())
        }
    }
}

impl PersistentState for Blocks {
    fn save_to_disk(&self, path: &str) -> Result<(), PersistentStateError> {
        let serialized_state = bincode::serialize(self)?;
        let mut file = File::create(path)?;
        file.write_all(&serialized_state)?;
        Ok(())
    }

    fn load_from_disk(path: &str) -> Result<Self, PersistentStateError> {
        if Path::new(path).exists() {
            let mut file = File::open(path)?;
            let mut data = Vec::new();
            file.read_to_end(&mut data)?;
            let new_state: Vec<BlockAny> = bincode::deserialize(&data)?;
            Ok(new_state)
        } else {
            Ok(Self::new())
        }
    }
}

impl Default for BlockHandler {
    fn default() -> Self {
        Self::new()
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
        blockhash.insert(block.header.hash(), blocks.len() - 1);
    }

    pub fn flush(&self) -> Result<(), PersistentStateError> {
        self.block_map
            .write()
            .unwrap()
            .save_to_disk(BLOCK_MAP_PATH)?;
        self.blocks.write().unwrap().save_to_disk(BLOCK_DATA_PATH)
    }

    pub fn get_block_by_hash(&self, hash: H256) -> Result<BlockAny, BlockHandlerError> {
        let block_map = self.block_map.read().unwrap();
        let block_number = *block_map
            .get(&hash)
            .ok_or(BlockHandlerError::BlockNotFound)?;

        let blocks = self.blocks.read().unwrap();
        let block = blocks
            .get(block_number)
            .ok_or(BlockHandlerError::BlockNotFound)?
            .clone();

        Ok(block)
    }

    pub fn get_block_by_number(&self, count: usize) -> Result<BlockAny, BlockHandlerError> {
        let blocks = self.blocks.read().unwrap();
        let block = blocks
            .get(count)
            .ok_or(BlockHandlerError::BlockNotFound)?
            .clone();

        Ok(block)
    }
}

use std::fmt;

#[derive(Debug)]
pub enum BlockHandlerError {
    BlockNotFound,
}

impl fmt::Display for BlockHandlerError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BlockHandlerError::BlockNotFound => write!(f, "Block not found"),
        }
    }
}

impl Error for BlockHandlerError {}
