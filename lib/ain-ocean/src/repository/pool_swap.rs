use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::{InitialKeyProvider, RepositoryOps};
use crate::{
    model::{PoolSwap, PoolSwapKey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "PoolSwapKey", V = "PoolSwap")]
pub struct PoolSwapRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwap>,
}

impl InitialKeyProvider<PoolSwapKey, PoolSwap> for PoolSwapRepository {
    type PartialKey = u32;
    fn initial_key(pk: Self::PartialKey) -> PoolSwapKey {
        (pk, u32::MAX, usize::MAX)
    }
}
