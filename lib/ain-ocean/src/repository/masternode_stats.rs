use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::MasternodeStats,
    storage::{columns, ocean_store::OceanStore, SortOrder},
    Result,
};

#[derive(Repository)]
#[repository(K = "u32", V = "MasternodeStats")]
pub struct MasternodeStatsRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::MasternodeStats>,
}

impl MasternodeStatsRepository {
    pub fn get_latest(&self) -> Result<Option<MasternodeStats>> {
        match self.col.iter(None, SortOrder::Descending.into())?.next() {
            None => Ok(None),
            Some(Ok((_, id))) => Ok(Some(id)),
            Some(Err(e)) => Err(e.into()),
        }
    }
}
