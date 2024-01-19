use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::TransactionVout,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "String", V = "TransactionVout")]
pub struct TransactionVoutRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TransactionVout>,
}

impl TransactionVoutRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
