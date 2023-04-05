use evm::{
    backend::{MemoryBackend, MemoryVicinity},
    executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata},
    ExitReason,
};
use primitive_types::H160;
use primitive_types::{H256, U256};
use std::collections::BTreeMap;
use std::error::Error;
use std::sync::{Arc, RwLock};

use crate::traits::PersistentState;
use crate::{EVMState, CONFIG, EVM_STATE_PATH, GAS_LIMIT};
use crate::block::BlockHandler;
use crate::evm::EVMHandler;

pub struct Handlers {
    pub evm: EVMHandler,
    pub block: BlockHandler,
}

impl Handlers {
    pub fn new() -> Self {
        Self {
            evm: EVMHandler::new(),
            block: BlockHandler::new(),
        }
    }
}
