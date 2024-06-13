use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::BlockHash;

use super::RepositoryOps;
use crate::{
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "BlockHash", V = "String")]
pub struct RawBlockRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::RawBlock>,
}
