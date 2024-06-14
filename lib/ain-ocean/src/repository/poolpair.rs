use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    storage::{columns, ocean_store::OceanStore},
    Result,
};

type PoolPairByHeightKey = (u32, usize); // block_height, tx_idx
type PoolPairValue = (u32, u32, u32); // pool_id, id_token_a, id_token_b

#[derive(Repository)]
#[repository(K = "PoolPairByHeightKey", V = "PoolPairValue")]
pub struct PoolPairByHeightRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolPairByHeight>,
}

type PoolPairId = (u32, u32);

#[derive(Repository)]
#[repository(K = "PoolPairId", V = "u32")]
pub struct PoolPairRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolPair>,
}
