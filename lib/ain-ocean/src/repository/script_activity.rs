use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::{RepositoryOps, SecondaryIndex};
use crate::{
    model::{ScriptActivity, ScriptActivityId},
    storage::{columns, ocean_store::OceanStore},
    Error, Result,
};

#[derive(Repository)]
#[repository(K = "ScriptActivityId", V = "ScriptActivity")]
pub struct ScriptActivityRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::ScriptActivity>,
}

#[derive(Repository)]
#[repository(K = "String", V = "ScriptActivityId")]
pub struct ScriptActivityKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::ScriptActivityKey>,
}

impl SecondaryIndex<String, ScriptActivityId> for ScriptActivityKeyRepository {
  type Value = ScriptActivity;

  fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
    let (_, id) = el?;

    let col = self.store.column::<columns::ScriptActivity>();
    let res = col.get(&id)?.ok_or(Error::SecondaryIndex)?;

    Ok(res)
  }
}
