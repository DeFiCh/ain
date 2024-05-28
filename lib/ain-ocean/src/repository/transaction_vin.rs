use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::{InitialKeyProvider, RepositoryOps};
use crate::{
    model::TransactionVin,
    storage::{
        columns::{self},
        ocean_store::OceanStore,
    },
    Result,
};

#[derive(Repository)]
#[repository(K = "String", V = "TransactionVin")]
pub struct TransactionVinRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TransactionVin>,
}

impl InitialKeyProvider<String, TransactionVin> for TransactionVinRepository {
    type PartialKey = Txid;

    fn initial_key(pk: Self::PartialKey) -> String {
        format!("{}00", pk)
    }
}
