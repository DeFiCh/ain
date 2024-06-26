use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::{RepositoryOps, SecondaryIndex};
use crate::{
    model::{ScriptUnspent, ScriptUnspentId},
    storage::{columns, ocean_store::OceanStore},
    Error, Result,
};

#[derive(Repository)]
#[repository(K = "ScriptUnspentId", V = "ScriptUnspent")]
pub struct ScriptUnspentRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::ScriptUnspent>,
}

#[derive(Repository)]
#[repository(K = "String", V = "ScriptUnspentId")]
pub struct ScriptUnspentKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::ScriptUnspentKey>,
}

impl SecondaryIndex<String, ScriptUnspentId> for ScriptUnspentKeyRepository {
    type Value = ScriptUnspent;

    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
        let (_, id) = el?;

        let col = self.store.column::<columns::ScriptUnspent>();
        let res = col.get(&id)?.ok_or(Error::SecondaryIndex)?;

        Ok(res)
    }
}
