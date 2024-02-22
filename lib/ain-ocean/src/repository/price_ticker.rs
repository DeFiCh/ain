use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    api::prices::PriceKey,
    model::{PriceTicker, PriceTickerId},
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
#[repository(K = "PriceKey", V = "PriceTickerId")]
pub struct PriceTickerRepositoryKey {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::PriceTickerKey>,
}

impl PriceTickerRepositoryKey {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
