use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::BlockHash;

use super::{RepositoryOps, SecondaryIndex};
use crate::{
    model::Block,
    storage::{columns, ocean_store::OceanStore, SortOrder},
    Error, Result,
};

#[derive(Repository)]
#[repository(K = "BlockHash", V = "Block")]
pub struct BlockRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::Block>,
}

impl BlockRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "u32", V = "BlockHash")]
pub struct BlockByHeightRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::BlockByHeight>,
}

impl BlockByHeightRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

impl BlockByHeightRepository {
    pub fn get_highest(&self) -> Result<Option<Block>> {
        match self.col.iter(None, SortOrder::Descending.into())?.next() {
            None => Ok(None),
            Some(Ok((_, id))) => {
                let col = self.store.column::<columns::Block>();
                Ok(col.get(&id)?)
            }
            Some(Err(e)) => Err(e.into()),
        }
    }
}

impl SecondaryIndex<u32, BlockHash> for BlockByHeightRepository {
    type Value = Block;

    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
        let (_, id) = el?;

        let col = self.store.column::<columns::Block>();
        let tx = col.get(&id)?.ok_or(Error::SecondaryIndex)?;

        Ok(tx)
    }
}
