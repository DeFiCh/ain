use std::sync::Arc;

use ain_macros::Repository;
use bitcoin::BlockHash;

use super::RepositoryOps;
use crate::{
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "BlockHash", V = "String", Column = "RawBlock")]
pub struct RawBlockRepository {
    pub store: Arc<OceanStore>,
}

impl RawBlockRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self { store }
    }
}
