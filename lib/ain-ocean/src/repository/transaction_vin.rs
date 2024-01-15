use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::TransactionVin,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "TransactionVin")]
pub struct TransactionVinRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TransactionVin>,
}

impl TransactionVinRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
