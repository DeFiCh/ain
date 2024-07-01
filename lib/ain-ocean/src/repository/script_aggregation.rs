use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{ScriptAggregation, ScriptAggregationId},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "ScriptAggregationId", V = "ScriptAggregation")]
pub struct ScriptAggregationRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::ScriptAggregation>,
}
