use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{PoolSwapAggregated, PoolSwapAggregatedId, PoolSwapAggregatedKey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

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
