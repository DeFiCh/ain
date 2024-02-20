pub mod consts;
pub mod error;
mod indexer;
use std::{path::PathBuf, sync::Arc};

pub use api::ocean_router;
use error::Error;
pub use indexer::{
    index_block, invalidate_block, oracle::invalidate_oracle_interval,
    transaction::index_transaction, tx_result,
};
use repository::{
    AuctionHistoryByHeightRepository, AuctionHistoryRepository, BlockByHeightRepository,
    BlockRepository, MasternodeByHeightRepository, MasternodeRepository, MasternodeStatsRepository,
    OracleHistoryRepository, OraclePriceActiveRepository,
    OraclePriceAggregatedIntervalKeyRepository, OraclePriceAggregatedIntervalRepository,
    OraclePriceAggregatedRepository, OraclePriceAggregatedRepositorykey,
    OraclePriceFeedKeyRepository, OraclePriceFeedRepository, OracleRepository,
    OracleTokenCurrencyKeyRepository, OracleTokenCurrencyRepository, PoolSwapRepository,
    RawBlockRepository, TransactionRepository, TransactionVinRepository, TransactionVoutRepository,
    TxResultRepository,TransactionByBlockHashRepository

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

pub struct OracleService {
    by_id: OracleRepository,
}
pub struct OraclePriceFeedService {
    by_key: OraclePriceFeedKeyRepository,
    by_id: OraclePriceFeedRepository,
}
pub struct OraclePriceActiveService {
    by_key: OraclePriceActiveRepository,
    by_id: OraclePriceActiveRepository,
}
pub struct OraclePriceAggregatedIntervalService {
    by_key: OraclePriceAggregatedIntervalKeyRepository,
    by_id: OraclePriceAggregatedIntervalRepository,
}
pub struct OraclePriceAggregatedService {
    by_key: OraclePriceAggregatedRepositorykey,
    by_id: OraclePriceAggregatedRepository,
}

pub struct OracleTokenCurrencyService {
    by_key: OracleTokenCurrencyKeyRepository,
    by_id: OracleTokenCurrencyRepository,
}

pub struct OracleHistoryService {
    by_id: OracleHistoryRepository,
}

pub struct Services {
    pub masternode: MasternodeService,
    pub block: BlockService,
    pub auction: AuctionService,
    pub result: TxResultRepository,
    pub pool: PoolService,
    pub transaction: TransactionService,
    oracle: OracleService,
    oracle_price_feed: OraclePriceFeedService,
    oracle_price_active: OraclePriceActiveService,
    oracle_price_aggregated_interval: OraclePriceAggregatedIntervalService,
    oracle_price_aggregated: OraclePriceAggregatedService,
    oracle_token_currency: OracleTokenCurrencyService,
    oracle_history: OracleHistoryService,
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
            oracle: OracleService {
                by_id: OracleRepository::new(Arc::clone(&store)),
            },
            oracle_price_feed: OraclePriceFeedService {
                by_key: OraclePriceFeedKeyRepository::new(Arc::clone(&store)),
                by_id: OraclePriceFeedRepository::new(Arc::clone(&store)),
            },
            oracle_price_active: OraclePriceActiveService {
                by_key: OraclePriceActiveRepository::new(Arc::clone(&store)),
                by_id: OraclePriceActiveRepository::new(Arc::clone(&store)),
            },
            oracle_price_aggregated_interval: OraclePriceAggregatedIntervalService {
                by_key: OraclePriceAggregatedIntervalKeyRepository::new(Arc::clone(&store)),
                by_id: OraclePriceAggregatedIntervalRepository::new(Arc::clone(&store)),
            },
            oracle_price_aggregated: OraclePriceAggregatedService {
                by_key: OraclePriceAggregatedRepositorykey::new(Arc::clone(&store)),
                by_id: OraclePriceAggregatedRepository::new(Arc::clone(&store)),
            },
            oracle_token_currency: OracleTokenCurrencyService {
                by_key: OracleTokenCurrencyKeyRepository::new(Arc::clone(&store)),
                by_id: OracleTokenCurrencyRepository::new(Arc::clone(&store)),
            },
            oracle_history: OracleHistoryService {
                by_id: OracleHistoryRepository::new(Arc::clone(&store)),
            },
        }
    }
}
