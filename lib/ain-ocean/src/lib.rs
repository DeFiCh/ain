pub mod error;
pub mod hex_encoder;
mod indexer;
pub mod network;

use std::{path::PathBuf, sync::Arc};

pub use api::ocean_router;
use error::Error;
pub use indexer::{
    index_block, invalidate_block, oracle::invalidate_oracle_interval,
    transaction::index_transaction, tx_result, PoolCreationHeight,
};
use parking_lot::Mutex;
use petgraph::graphmap::UnGraphMap;
use repository::{
    AuctionHistoryByHeightRepository, AuctionHistoryRepository, BlockByHeightRepository,
    BlockRepository, MasternodeByHeightRepository, MasternodeRepository, MasternodeStatsRepository,
    OracleHistoryRepository, OracleHistoryRepositoryKey, OraclePriceActiveKeyRepository,
    OraclePriceActiveRepository, OraclePriceAggregatedIntervalKeyRepository,
    OraclePriceAggregatedIntervalRepository, OraclePriceAggregatedRepository,
    OraclePriceAggregatedRepositorykey, OraclePriceFeedKeyRepository, OraclePriceFeedRepository,
    OracleRepository, OracleTokenCurrencyKeyRepository, OracleTokenCurrencyRepository,
    PoolPairByHeightRepository, PoolPairRepository, PoolSwapAggregatedKeyRepository,
    PoolSwapAggregatedRepository, PoolSwapRepository, PriceTickerKeyRepository,
    PriceTickerRepository, RawBlockRepository, ScriptActivityKeyRepository,
    ScriptActivityRepository, ScriptAggregationRepository, ScriptUnspentKeyRepository,
    ScriptUnspentRepository, TransactionByBlockHashRepository, TransactionRepository,
    TransactionVinRepository, TransactionVoutRepository, TxResultRepository,
};
use serde::Serialize;
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

pub struct PoolPairService {
    by_height: PoolPairByHeightRepository,
    by_id: PoolPairRepository,
}

pub struct PoolSwapAggregatedService {
    by_id: PoolSwapAggregatedRepository,
    by_key: PoolSwapAggregatedKeyRepository,
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
    by_key: OraclePriceActiveKeyRepository,
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
    by_key: OracleHistoryRepositoryKey,
}

pub struct PriceTickerService {
    by_id: PriceTickerRepository,
    by_key: PriceTickerKeyRepository,
}

pub struct ScriptActivityService {
    by_id: ScriptActivityRepository,
    by_key: ScriptActivityKeyRepository,
}

pub struct ScriptAggregationService {
    by_id: ScriptAggregationRepository,
}

pub struct ScriptUnspentService {
    by_id: ScriptUnspentRepository,
    by_key: ScriptUnspentKeyRepository,
}

#[derive(Clone, Debug, Serialize, Eq, PartialEq, Hash)]
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
    pub poolpair: PoolPairService,
    pub pool_swap_aggregated: PoolSwapAggregatedService,
    pub transaction: TransactionService,
    pub oracle: OracleService,
    pub oracle_price_feed: OraclePriceFeedService,
    pub oracle_price_active: OraclePriceActiveService,
    pub oracle_price_aggregated_interval: OraclePriceAggregatedIntervalService,
    pub oracle_price_aggregated: OraclePriceAggregatedService,
    pub oracle_token_currency: OracleTokenCurrencyService,
    pub oracle_history: OracleHistoryService,
    pub price_ticker: PriceTickerService,
    pub script_activity: ScriptActivityService,
    pub script_aggregation: ScriptAggregationService,
    pub script_unspent: ScriptUnspentService,
    pub token_graph: Arc<Mutex<UnGraphMap<u32, String>>>,
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
            poolpair: PoolPairService {
                by_height: PoolPairByHeightRepository::new(Arc::clone(&store)),
                by_id: PoolPairRepository::new(Arc::clone(&store)),
            },
            pool: PoolService {
                by_id: PoolSwapRepository::new(Arc::clone(&store)),
            },
            pool_swap_aggregated: PoolSwapAggregatedService {
                by_id: PoolSwapAggregatedRepository::new(Arc::clone(&store)),
                by_key: PoolSwapAggregatedKeyRepository::new(Arc::clone(&store)),
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
                by_key: OraclePriceActiveKeyRepository::new(Arc::clone(&store)),
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
                by_key: OracleHistoryRepositoryKey::new(Arc::clone(&store)),
            },
            price_ticker: PriceTickerService {
                by_id: PriceTickerRepository::new_id(Arc::clone(&store)),
                by_key: PriceTickerKeyRepository::new_key(Arc::clone(&store)),
            },
            script_activity: ScriptActivityService {
                by_id: ScriptActivityRepository::new(Arc::clone(&store)),
                by_key: ScriptActivityKeyRepository::new(Arc::clone(&store)),
            },
            script_aggregation: ScriptAggregationService {
                by_id: ScriptAggregationRepository::new(Arc::clone(&store)),
            },
            script_unspent: ScriptUnspentService {
                by_id: ScriptUnspentRepository::new(Arc::clone(&store)),
                by_key: ScriptUnspentKeyRepository::new(Arc::clone(&store)),
            },
            token_graph: Arc::new(Mutex::new(UnGraphMap::new())),
        }
    }
}
