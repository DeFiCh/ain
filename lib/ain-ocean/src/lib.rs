pub mod error;
mod indexer;

use std::{collections::HashMap, path::PathBuf, sync::Arc};
use parking_lot::Mutex;
use petgraph::graphmap::UnGraphMap;
use serde::Serialize;

pub use api::{ocean_router, common::parse_display_symbol};
use error::Error;
pub use indexer::{index_block, invalidate_block, transaction::index_transaction, tx_result};
use repository::{
    AuctionHistoryByHeightRepository, AuctionHistoryRepository, BlockByHeightRepository,
    BlockRepository, MasternodeByHeightRepository, MasternodeRepository, MasternodeStatsRepository,
    PoolSwapRepository, RawBlockRepository, TransactionByBlockHashRepository,
    TransactionRepository, TransactionVinRepository, TransactionVoutRepository, TxResultRepository,
};
pub mod api;
mod model;
mod repository;
pub mod storage;

use crate::storage::ocean_store::OceanStore;

pub type Result<T> = std::result::Result<T, Error>;

lazy_static::lazy_static! {
    // Global services exposed by the library
    pub static ref SERVICES: Arc<Services> = {
        let datadir = ain_cpp_imports::get_datadir();
        let store = Arc::new(OceanStore::new(&PathBuf::from(datadir)).expect("Error initialization Ocean services"));

        Arc::new(Services::new(store))
    };
}

pub struct MasternodeService {
    pub by_id: MasternodeRepository,
    pub by_height: MasternodeByHeightRepository,
    pub stats: MasternodeStatsRepository,
}

pub struct BlockService {
    pub raw: RawBlockRepository,
    pub by_id: BlockRepository,
    pub by_height: BlockByHeightRepository,
}

pub struct AuctionService {
    by_id: AuctionHistoryRepository,
    by_height: AuctionHistoryByHeightRepository,
}

pub struct PoolService {
    by_id: PoolSwapRepository,
}

pub struct TransactionService {
    by_id: TransactionRepository,
    by_block_hash: TransactionByBlockHashRepository,
    vin_by_id: TransactionVinRepository,
    vout_by_id: TransactionVoutRepository,
}


#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TokenIdentifier {
    pub id: String,
    pub name: String,
    pub symbol: String,
    pub display_symbol: String,
}

pub struct Services {
    pub masternode: MasternodeService,
    pub block: BlockService,
    pub auction: AuctionService,
    pub result: TxResultRepository,
    pub pool: PoolService,
    pub transaction: TransactionService,
    pub token_graph: Arc<Mutex<UnGraphMap<u32, String>>>,
    pub tokens_to_swappable_tokens: Arc<Mutex<HashMap<String, Vec<TokenIdentifier>>>>,
}

impl Services {
    pub fn new(store: Arc<OceanStore>) -> Self {
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
            auction: AuctionService {
                by_id: AuctionHistoryRepository::new(Arc::clone(&store)),
                by_height: AuctionHistoryByHeightRepository::new(Arc::clone(&store)),
            },
            result: TxResultRepository::new(Arc::clone(&store)),
            pool: PoolService {
                by_id: PoolSwapRepository::new(Arc::clone(&store)),
            },
            transaction: TransactionService {
                by_id: TransactionRepository::new(Arc::clone(&store)),
                by_block_hash: TransactionByBlockHashRepository::new(Arc::clone(&store)),
                vin_by_id: TransactionVinRepository::new(Arc::clone(&store)),
                vout_by_id: TransactionVoutRepository::new(Arc::clone(&store)),
            },
            token_graph: Arc::new(Mutex::new(UnGraphMap::new())),
            tokens_to_swappable_tokens: Arc::new(Mutex::new(HashMap::new())),
        }
    }
}
