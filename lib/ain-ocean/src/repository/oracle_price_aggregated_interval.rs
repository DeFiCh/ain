use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{
        OraclePriceAggregatedInterval, OraclePriceAggregatedIntervalId,
        OraclePriceAggregatedIntervalKey,
    },
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(
    K = "OraclePriceAggregatedIntervalId",
    V = "OraclePriceAggregatedInterval"
)]
pub struct OraclePriceAggregatedIntervalRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceAggregatedInterval>,
}

impl OraclePriceAggregatedIntervalRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(
    K = "OraclePriceAggregatedIntervalKey",
    V = "OraclePriceAggregatedIntervalId"
)]
pub struct OraclePriceAggregatedIntervalKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceAggregatedIntervalKey>,
}

impl OraclePriceAggregatedIntervalKeyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
