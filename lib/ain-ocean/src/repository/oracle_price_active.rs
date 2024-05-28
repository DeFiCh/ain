use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{OraclePriceActive, OraclePriceActiveId, OraclePriceActiveKey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "OraclePriceActiveId", V = "OraclePriceActive")]
pub struct OraclePriceActiveRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceActive>,
}

#[derive(Repository)]
#[repository(K = "OraclePriceActiveKey", V = "OraclePriceActiveId")]
pub struct OraclePriceActiveKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceActiveKey>,
}
