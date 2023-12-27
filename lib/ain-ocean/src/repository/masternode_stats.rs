use std::sync::Arc;

use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::MasternodeStats,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "MasternodeStats", Column = "MasternodeStats")]
pub struct MasternodeStatsRepository {
    pub store: Arc<OceanStore>,
}

impl MasternodeStatsRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self { store }
    }
}
