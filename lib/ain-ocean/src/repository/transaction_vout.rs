use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{TransactionVout, TransactionVoutKey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "TransactionVoutKey", V = "TransactionVout")]
pub struct TransactionVoutRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TransactionVout>,
}
