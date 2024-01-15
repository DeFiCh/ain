use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::{BlockHash, Txid};

use super::RepositoryOps;
use crate::{
    model::Transaction,
    storage::{columns, ocean_store::OceanStore},
    Result,
};
#[derive(Repository)]
#[repository(K = "String", V = "Transaction")]
pub struct TransactionRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::Transaction>,
}

impl TransactionRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
