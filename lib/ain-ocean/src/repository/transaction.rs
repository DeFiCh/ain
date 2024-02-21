use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::{Transaction, TransactionByBlockHashKey},
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
#[repository(K = "TransactionByBlockHashKey", V = "Txid")]
pub struct TransactionByBlockHashRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TransactionByBlockHash>,
}

impl TransactionByBlockHashRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
