use std::sync::Arc;

use ain_macros::Repository;
use bitcoin::BlockHash;

use super::RepositoryOps;
use crate::{
    model::Block,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "BlockHash", V = "Block", Column = "Block")]
pub struct BlockRepository {
    pub store: Arc<OceanStore>,
}

impl BlockRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self { store }
    }
}

#[derive(Repository)]
#[repository(K = "u32", V = "BlockHash", Column = "BlockByHeight")]
pub struct BlockByHeightRepository {
    pub store: Arc<OceanStore>,
}

impl BlockByHeightRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self { store }
    }
}

impl BlockByHeightRepository {
    pub fn get_highest(&self) -> Result<Option<Block>> {
        match self.store.list::<columns::BlockByHeight>(None, 1)?.first() {
            None => Ok(None),
            Some((_, id)) => Ok(self.store.get::<columns::Block>(*id)?),
        }
    }
}
