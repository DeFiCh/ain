use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{PriceTicker, PriceTickerId, PriceTickerKey},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "PriceTickerId", V = "PriceTicker")]
pub struct PriceTickerRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PriceTicker>,
}

impl PriceTickerRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "PriceTickerKey", V = "PriceTickerId")]
pub struct PriceTickerKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PriceTickerKey>,
}

impl PriceTickerKeyRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
