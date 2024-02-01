use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{OracleId, OracleKey, OraclePriceFeed},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "OracleId", V = "OraclePriceFeed")]
pub struct OraclePriceFeedRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceFeed>,
}

impl OraclePriceFeedRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "OracleKey", V = "OracleId")]
pub struct OraclePriceFeedKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceFeedKey>,
}

impl OraclePriceFeedKeyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
