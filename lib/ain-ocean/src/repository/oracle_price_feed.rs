use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{OraclePriceFeed, OraclePriceFeedId, OraclePriceFeedkey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "OraclePriceFeedId", V = "OraclePriceFeed")]
pub struct OraclePriceFeedRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceFeed>,
}

#[derive(Repository)]
#[repository(K = "OraclePriceFeedkey", V = "OraclePriceFeedId")]
pub struct OraclePriceFeedKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceFeedKey>,
}
