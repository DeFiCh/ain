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

impl PoolSwapAggregatedRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "PoolSwapAggregatedKey", V = "PoolSwapAggregatedId")]
pub struct PoolSwapAggregatedKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwapAggregatedKey>,
}

impl PoolSwapAggregatedKeyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

impl SecondaryIndex<PoolSwapAggregatedKey, PoolSwapAggregatedId> for PoolSwapAggregatedKeyRepository {
	type Value = PoolSwapAggregated;

	fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
		let (_, id) = el?;

		let col = self.store.column::<columns::PoolSwapAggregated>();
		let res = col.get(&id)?.ok_or(Error::SecondaryIndex)?;

		Ok(res)
	}
}


#[derive(Repository)]
#[repository(K = "PoolSwapAggregatedId", V = "PoolSwapAggregated")]
pub struct PoolSwapAggregatedOneDayRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwapAggregatedOneDay>,
}

impl PoolSwapAggregatedOneDayRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "PoolSwapAggregatedKey", V = "String")]
pub struct PoolSwapAggregatedOneDayKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwapAggregatedOneDayKey>,
}

impl PoolSwapAggregatedOneDayKeyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "PoolSwapAggregatedId", V = "PoolSwapAggregated")]
pub struct PoolSwapAggregatedOneHourRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwapAggregatedOneHour>,
}

impl PoolSwapAggregatedOneHourRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "PoolSwapAggregatedKey", V = "String")]
pub struct PoolSwapAggregatedOneHourKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PoolSwapAggregatedOneHourKey>,
}

impl PoolSwapAggregatedOneHourKeyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
