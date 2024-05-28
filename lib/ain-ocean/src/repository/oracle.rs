use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::Oracle,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "Oracle")]
pub struct OracleRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::Oracle>,
}
