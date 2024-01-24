pub mod api_query;
pub mod error;
mod indexer;

use std::{path::PathBuf, sync::Arc};

pub use api::ocean_router;
use error::OceanError;
pub use indexer::{
    index_block, invalidate_block,
    transaction::{index_transaction, invalidate_transaction},
    tx_result, BlockV2Info,
};
use repository::{
    AuctionHistoryByHeightRepository, AuctionHistoryRepository, BlockByHeightRepository,
    BlockRepository, MasternodeByHeightRepository, MasternodeRepository, MasternodeStatsRepository,
    PoolSwapRepository, RawBlockRepository, TransactionRepository, TransactionVinRepository,
    TransactionVoutRepository, TxResultRepository,
    OraclePriceActiveRepository, OraclePriceAggregatedIntervalRepository,
    OraclePriceAggregatedRepository, OraclePriceFeedRepository, OracleTokenCurrencyRepository,
 
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
    pub static ref SERVICES: Arc<Services> = {
        let services = || {
            let datadir = ain_cpp_imports::get_datadir();
            let store = Arc::new(OceanStore::new(&PathBuf::from(datadir))?);

            let (user, pass) = ain_cpp_imports::get_rpc_auth()?;
            let client = Arc::new(Client::new(
                &format!("localhost:{}", ain_cpp_imports::get_rpc_port()),
                Auth::UserPass(user, pass),
            )?);

            Services::new(client, store)
        };

        Arc::new(services().expect("Error initialization Ocean services"))
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
    vin_by_id: TransactionVinRepository,
    vout_by_id: TransactionVoutRepository,
}

pub struct OraclePriceFeedService {
    by_key: OraclePriceFeedRepository,
    by_id: OraclePriceFeedRepository,
}
pub struct OraclePriceActiveService {
    by_key: OraclePriceActiveRepository,
    by_id: OraclePriceActiveRepository,
}
pub struct OraclePriceAggregatedIntervalService {
    by_key: OraclePriceAggregatedIntervalRepository,
    by_id: OraclePriceAggregatedIntervalRepository,
}
pub struct OraclePriceAggregatedService {
    by_key: OraclePriceAggregatedRepository,
    by_id: OraclePriceAggregatedRepository,
}

pub struct OracleTokenCurrencyService {
    by_key: OracleTokenCurrencyRepository,
    by_id: OracleTokenCurrencyRepository,
}
pub struct Services {
    masternode: MasternodeService,
    block: BlockService,
    auction: AuctionService,
    result: TxResultRepository,
    pool: PoolService,
    client: Arc<Client>,
    transaction: TransactionService,
    oracle_price_feed: OraclePriceFeedService,
    oracle_price_active: OraclePriceActiveService,
    oracle_price_aggregated_interval: OraclePriceAggregatedIntervalService,
    oracle_price_aggregated: OraclePriceAggregatedService,
    oracle_token_currency: OracleTokenCurrencyService,
}

impl Services {
    pub fn new(client: Arc<Client>, store: Arc<OceanStore>) -> Result<Self> {
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
                vin_by_id: TransactionVinRepository::new(Arc::clone(&store)),
                vout_by_id: TransactionVoutRepository::new(Arc::clone(&store)),
            },
            oracle_price_feed: OraclePriceFeedService {
                by_key: OraclePriceFeedRepository::new(Arc::clone(&store)),
                by_id: OraclePriceFeedRepository::new(Arc::clone(&store)),
            },
            oracle_price_active: OraclePriceActiveService {
                by_key: OraclePriceActiveRepository::new(Arc::clone(&store)),
                by_id: OraclePriceActiveRepository::new(Arc::clone(&store)),
            },
            oracle_price_aggregated_interval: OraclePriceAggregatedIntervalService {
                by_key: OraclePriceAggregatedIntervalRepository::new(Arc::clone(&store)),
                by_id: OraclePriceAggregatedIntervalRepository::new(Arc::clone(&store)),
            },
            oracle_price_aggregated: OraclePriceAggregatedService {
                by_key: OraclePriceAggregatedRepository::new(Arc::clone(&store)),
                by_id: OraclePriceAggregatedRepository::new(Arc::clone(&store)),
            },
            oracle_token_currency: OracleTokenCurrencyService {
                by_key: OracleTokenCurrencyRepository::new(Arc::clone(&store)),
                by_id: OracleTokenCurrencyRepository::new(Arc::clone(&store)),
            },
            client,
        })
    }
}
