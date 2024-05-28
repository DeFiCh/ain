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

#[derive(Repository)]
#[repository(K = "OracleTokenCurrencyKey", V = "OracleTokenCurrencyId")]
pub struct OracleTokenCurrencyKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OracleTokenCurrencyKey>,
}
