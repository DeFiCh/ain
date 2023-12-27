pub mod api_paged_response;
pub mod api_query;
pub mod error;
mod indexer;

use std::{path::PathBuf, sync::Arc};

pub use api::ocean_router;
pub use indexer::{index_block, invalidate_block};
use repository::{
    BlockByHeightRepository, BlockRepository, MasternodeByHeightRepository, MasternodeRepository,
    MasternodeStatsRepository, RawBlockRepository,
};
pub mod api;
mod model;
mod repository;
pub mod storage;
use crate::storage::ocean_store::OceanStore;

pub(crate) type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

lazy_static::lazy_static! {
    // Global services exposed by the library
    pub static ref SERVICES: Services = {
        let datadir = ain_cpp_imports::get_datadir();
        let store = OceanStore::new(&PathBuf::from(datadir)).expect("Error initialization Ocean storage");
        Services::new(
            Arc::new(store)
        )
    };
}

pub struct MasternodeService {
    by_id: MasternodeRepository,
    by_height: MasternodeByHeightRepository,
    stats: MasternodeStatsRepository,
}

pub struct BlockService {
    raw: RawBlockRepository,
    by_id: BlockRepository,
    by_height: BlockByHeightRepository,
}

pub struct Services {
    masternode: MasternodeService,
    block: BlockService,
}

impl Services {
    fn new(store: Arc<OceanStore>) -> Self {
        Self {
            masternode: MasternodeService {
                by_id: MasternodeRepository::new(Arc::clone(&store)),
                by_height: MasternodeByHeightRepository::new(Arc::clone(&store)),
                stats: MasternodeStatsRepository::new(Arc::clone(&store)),
            },
            block: BlockService {
                raw: RawBlockRepository::new(Arc::clone(&store)),
                by_id: BlockRepository::new(Arc::clone(&store)),
                by_height: BlockByHeightRepository::new(Arc::clone(&store)),
            },
        }
    }
}
