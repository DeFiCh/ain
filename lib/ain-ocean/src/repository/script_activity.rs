use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{ScriptActivity, ScriptActivityId},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "ScriptActivityId", V = "ScriptActivity")]
pub struct ScriptActivityRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::ScriptActivity>,
}