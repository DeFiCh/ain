use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::TxResult,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "TxResult")]
pub struct TxResultRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TxResult>,
}

impl TxResultRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
