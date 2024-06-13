use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::{RepositoryOps, SecondaryIndex};
use crate::{
    model::{PoolSwapAggregated, PoolSwapAggregatedId, PoolSwapAggregatedKey},
    storage::{columns, ocean_store::OceanStore},
    Error, Result,
};

#[derive(Repository)]
#[repository(K = "PoolSwapAggregatedId", V = "PoolSwapAggregated")]
pub struct PoolSwapAggregatedRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwapAggregated>,
}

#[derive(Repository)]
#[repository(K = "PoolSwapAggregatedKey", V = "PoolSwapAggregatedId")]
pub struct PoolSwapAggregatedKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwapAggregatedKey>,
}

impl SecondaryIndex<PoolSwapAggregatedKey, PoolSwapAggregatedId>
    for PoolSwapAggregatedKeyRepository
{
    type Value = PoolSwapAggregated;

    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
        let (_, id) = el?;

        let col = self.store.column::<columns::PoolSwapAggregated>();
        let res = col.get(&id)?.ok_or(Error::SecondaryIndex)?;

        Ok(res)
    }
}
