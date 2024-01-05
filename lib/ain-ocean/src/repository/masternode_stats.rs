use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::MasternodeStats,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "MasternodeStats")]
pub struct MasternodeStatsRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::MasternodeStats>,
}

impl MasternodeStatsRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}
