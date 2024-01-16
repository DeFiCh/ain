pub mod api_query;
pub mod error;
mod indexer;

use std::{path::PathBuf, sync::Arc};

pub use api::ocean_router;
use error::OceanError;
pub use indexer::{index_block, invalidate_block, tx_result, BlockV2Info};
use model::TransactionVin;
use repository::{
    AuctionHistoryByHeightRepository, AuctionHistoryRepository, BlockByHeightRepository,
    BlockRepository, MasternodeByHeightRepository, MasternodeRepository, MasternodeStatsRepository,
    PoolSwapRepository, RawBlockRepository, TransactionBlockHashRepository, TransactionRepository,
    TransactionVinRepository, TransactionVoutRepository, TxResultRepository,
};
pub mod api;
mod model;
mod repository;
pub mod storage;
use defichain_rpc::{Auth, Client};

use crate::storage::ocean_store::OceanStore;

pub type Result<T> = std::result::Result<T, OceanError>;

lazy_static::lazy_static! {
    // Global services exposed by the library
    pub static ref SERVICES: Services = {
        Services::new().expect("Error initialization Ocean services")
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

pub struct AuctionService {
    by_id: AuctionHistoryRepository,
    by_height: AuctionHistoryByHeightRepository,
}

pub struct PoolService {
    by_id: PoolSwapRepository,
}

pub struct TransactionService {
    by_id: TransactionRepository,
    by_block_hash: TransactionBlockHashRepository,
    vin_by_id: TransactionVinRepository,
    vout_by_id: TransactionVoutRepository,
}

pub struct Services {
    masternode: MasternodeService,
    block: BlockService,
    auction: AuctionService,
    result: TxResultRepository,
    pool: PoolService,
    client: Arc<Client>,
    transaction: TransactionService,
}

impl Services {
    fn new() -> Result<Self> {
        let datadir = ain_cpp_imports::get_datadir();
        let store = Arc::new(OceanStore::new(&PathBuf::from(datadir))?);

        let (user, pass) = ain_cpp_imports::get_rpc_auth()?;
        let client = Arc::new(Client::new(
            &format!("localhost:{}", ain_cpp_imports::get_rpc_port()),
            Auth::UserPass(user, pass),
        )?);

        Ok(Self {
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
                by_block_hash: TransactionBlockHashRepository::new(Arc::clone(&store)),
                vin_by_id: TransactionVinRepository::new(Arc::clone(&store)),
                vout_by_id: TransactionVoutRepository::new(Arc::clone(&store)),
            },
            client,
        })
    }
}
