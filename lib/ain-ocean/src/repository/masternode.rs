use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::Txid;

use super::{RepositoryOps, SecondaryIndex};
use crate::{
    model::Masternode,
    storage::{columns, ocean_store::OceanStore},
    Error, Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "Masternode")]
pub struct MasternodeRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::Masternode>,
}

type MasternodeByHeightKey = (u32, Txid);

#[derive(Repository)]
#[repository(K = "MasternodeByHeightKey", V = "u8")]
pub struct MasternodeByHeightRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::MasternodeByHeight>,
}

impl SecondaryIndex<MasternodeByHeightKey, u8> for MasternodeByHeightRepository {
    type Value = Masternode;

    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
        let ((_, id), _) = el?;
        let col = self.store.column::<columns::Masternode>();
        let tx = col.get(&id)?.ok_or(Error::SecondaryIndex)?;
        Ok(tx)
    }
}
