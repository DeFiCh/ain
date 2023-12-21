use std::sync::Arc;

use ain_macros::Repository;
use bitcoin::Txid;

use super::RepositoryOps;
use crate::{
    model::masternode::Masternode,
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "Masternode", Column = "Masternode")]
pub struct MasternodeRepository {
    pub store: Arc<OceanStore>,
}

impl MasternodeRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self { store }
    }
}
