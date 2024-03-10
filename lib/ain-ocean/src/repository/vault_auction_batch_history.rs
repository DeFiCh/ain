use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;

use super::RepositoryOps;
use crate::{
    model::{AuctionHistoryByHeightKey, AuctionHistoryKey, VaultAuctionBatchHistory},
    storage::{columns, ocean_store::OceanStore},
    Result,
};

#[derive(Repository)]
#[repository(K = "AuctionHistoryKey", V = "VaultAuctionBatchHistory")]
pub struct AuctionHistoryRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::VaultAuctionHistory>,
}

impl AuctionHistoryRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

#[derive(Repository)]
#[repository(K = "AuctionHistoryByHeightKey", V = "AuctionHistoryKey")]
pub struct AuctionHistoryByHeightRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::VaultAuctionHistoryByHeight>,
}

impl AuctionHistoryByHeightRepository {
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            col: store.column(),
            store,
        }
    }
}

impl AuctionHistoryByHeightRepository {
    // pub fn get_latest(&self) -> Result<Option<VaultAuctionBatchHistory>> {
    //     match self.list(None::Descending)?.next() {
    //         None => Ok(None),
    //         Some(Ok((_, id))) => {
    //             let col = self.store.column::<columns::VaultAuctionHistory>();
    //             Ok(col.get(&id)?)
    //         }
    //         Some(Err(e)) => Err(e.into()),
    //     }
    // }
}
