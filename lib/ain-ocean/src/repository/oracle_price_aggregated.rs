use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{OraclePriceAggregated, OraclePriceAggregatedId, OraclePriceAggregatedKey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "OraclePriceAggregatedId", V = "OraclePriceAggregated")]
pub struct OraclePriceAggregatedRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceAggregated>,
}

impl OraclePriceAggregatedRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "OraclePriceAggregatedKey", V = "OraclePriceAggregatedId")]
pub struct OraclePriceAggregatedRepositorykey {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceAggregatedKey>,
}

impl OraclePriceAggregatedRepositorykey {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}