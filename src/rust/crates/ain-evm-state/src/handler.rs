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
