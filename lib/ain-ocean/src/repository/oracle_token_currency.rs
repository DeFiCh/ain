use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{OracleTokenCurrency, OracleTokenCurrencyId, OracleTokenCurrencyKey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "OracleTokenCurrencyId", V = "OracleTokenCurrency")]
pub struct OracleTokenCurrencyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OracleTokenCurrency>,
}

impl OracleTokenCurrencyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "OracleTokenCurrencyKey", V = "OracleTokenCurrencyId")]
pub struct OracleTokenCurrencyKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OracleTokenCurrencyKey>,
}

impl OracleTokenCurrencyKeyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
