use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    storage::{columns, ocean_store::OceanStore},
    Result,
};

type PoolPairKey = (u32, usize);

#[derive(Repository)]
#[repository(K = "PoolPairKey", V = "u32")]
pub struct PoolPairRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolPair>,
}
