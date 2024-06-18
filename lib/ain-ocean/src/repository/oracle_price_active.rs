use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::{RepositoryOps, SecondaryIndex};
use crate::{
    model::{OraclePriceActive, OraclePriceActiveId, OraclePriceActiveKey},
    storage::{columns, ocean_store::OceanStore},
    Error, Result,
};

#[derive(Repository)]
#[repository(K = "OraclePriceActiveId", V = "OraclePriceActive")]
pub struct OraclePriceActiveRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceActive>,
}

#[derive(Repository)]
#[repository(K = "OraclePriceActiveKey", V = "OraclePriceActiveId")]
pub struct OraclePriceActiveKeyRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::OraclePriceActiveKey>,
}

impl SecondaryIndex<OraclePriceActiveKey, OraclePriceActiveId> for OraclePriceActiveKeyRepository {
    type Value = OraclePriceActive;

    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
        let (_, id) = el?;

        let col = self.store.column::<columns::OraclePriceActive>();
        let res = col.get(&id)?.ok_or(Error::SecondaryIndex)?;

        Ok(res)
    }
}