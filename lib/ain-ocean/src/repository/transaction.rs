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
#[repository(K = "Txid", V = "Transaction")]
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

#[derive(Repository)]
#[repository(K = "BlockHash", V = "Transaction")]
pub struct TransactionBlockHashRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TransactionByBlockHash>,
}

impl TransactionBlockHashRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
