pub mod error;
pub mod hex_encoder;
mod indexer;
pub mod network;
mod storage;

use std::{path::PathBuf, sync::Arc};

pub use api::ocean_router;
use error::Error;
pub use indexer::{
    index_block, invalidate_block,
    oracle::invalidate_oracle_interval,
    transaction::{index_transaction, invalidate_transaction},
    tx_result, PoolCreationHeight,
};
use log::debug;
use parking_lot::Mutex;
use petgraph::graphmap::UnGraphMap;
use serde::Serialize;
pub mod api;
mod model;

use storage::*;

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
    pub by_id: Masternode,
    pub by_height: MasternodeByHeight,
    pub stats: MasternodeStats,
}

pub struct BlockService {
    pub raw: RawBlock,
    pub by_id: Block,
    pub by_height: BlockByHeight,
}

pub struct AuctionService {
    by_id: VaultAuctionHistory,
    by_height: VaultAuctionHistoryByHeight,
}

pub struct PoolService {
    by_id: PoolSwap,
}

pub struct PoolPairService {
    by_height: PoolPairByHeight,
    by_id: PoolPair,
}

pub struct PoolSwapAggregatedService {
    by_id: PoolSwapAggregated,
    by_key: PoolSwapAggregatedKey,
}

pub struct TransactionService {
    by_id: Transaction,
    by_block_hash: TransactionByBlockHash,
    vin_by_id: TransactionVin,
    vout_by_id: TransactionVout,
}

pub struct OracleService {
    by_id: Oracle,
}
pub struct OraclePriceFeedService {
    by_key: OraclePriceFeedKey,
    by_id: OraclePriceFeed,
}
pub struct OraclePriceActiveService {
    by_key: OraclePriceActiveKey,
    by_id: OraclePriceActive,
}
pub struct OraclePriceAggregatedIntervalService {
    by_key: OraclePriceAggregatedIntervalKey,
    by_id: OraclePriceAggregatedInterval,
}
pub struct OraclePriceAggregatedService {
    by_key: OraclePriceAggregatedKey,
    by_id: OraclePriceAggregated,
}

pub struct OracleTokenCurrencyService {
    by_key: OracleTokenCurrencyKey,
    by_id: OracleTokenCurrency,
}

pub struct OracleHistoryService {
    by_id: OracleHistory,
    by_key: OracleHistoryOracleIdSort,
}

pub struct PriceTickerService {
    by_id: PriceTicker,
    by_key: PriceTickerKey,
}

pub struct ScriptActivityService {
    by_id: ScriptActivity,
}

pub struct ScriptAggregationService {
    by_id: ScriptAggregation,
}

pub struct ScriptUnspentService {
    by_id: ScriptUnspent,
    by_key: ScriptUnspentKey,
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
    pub result: TxResult,
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
    pub store: Arc<OceanStore>,
}

impl Services {
    #[must_use]
    pub fn new(store: Arc<OceanStore>) -> Self {
        Self {
            masternode: MasternodeService {
                by_id: Masternode::new(Arc::clone(&store)),
                by_height: MasternodeByHeight::new(Arc::clone(&store)),
                stats: MasternodeStats::new(Arc::clone(&store)),
            },
            block: BlockService {
                raw: RawBlock::new(Arc::clone(&store)),
                by_id: Block::new(Arc::clone(&store)),
                by_height: BlockByHeight::new(Arc::clone(&store)),
            },
            auction: AuctionService {
                by_id: VaultAuctionHistory::new(Arc::clone(&store)),
                by_height: VaultAuctionHistoryByHeight::new(Arc::clone(&store)),
            },
            result: TxResult::new(Arc::clone(&store)),
            poolpair: PoolPairService {
                by_height: PoolPairByHeight::new(Arc::clone(&store)),
                by_id: PoolPair::new(Arc::clone(&store)),
            },
            pool: PoolService {
                by_id: PoolSwap::new(Arc::clone(&store)),
            },
            pool_swap_aggregated: PoolSwapAggregatedService {
                by_id: PoolSwapAggregated::new(Arc::clone(&store)),
                by_key: PoolSwapAggregatedKey::new(Arc::clone(&store)),
            },
            transaction: TransactionService {
                by_id: Transaction::new(Arc::clone(&store)),
                by_block_hash: TransactionByBlockHash::new(Arc::clone(&store)),
                vin_by_id: TransactionVin::new(Arc::clone(&store)),
                vout_by_id: TransactionVout::new(Arc::clone(&store)),
            },
            oracle: OracleService {
                by_id: Oracle::new(Arc::clone(&store)),
            },
            oracle_price_feed: OraclePriceFeedService {
                by_key: OraclePriceFeedKey::new(Arc::clone(&store)),
                by_id: OraclePriceFeed::new(Arc::clone(&store)),
            },
            oracle_price_active: OraclePriceActiveService {
                by_key: OraclePriceActiveKey::new(Arc::clone(&store)),
                by_id: OraclePriceActive::new(Arc::clone(&store)),
            },
            oracle_price_aggregated_interval: OraclePriceAggregatedIntervalService {
                by_key: OraclePriceAggregatedIntervalKey::new(Arc::clone(&store)),
                by_id: OraclePriceAggregatedInterval::new(Arc::clone(&store)),
            },
            oracle_price_aggregated: OraclePriceAggregatedService {
                by_key: OraclePriceAggregatedKey::new(Arc::clone(&store)),
                by_id: OraclePriceAggregated::new(Arc::clone(&store)),
            },
            oracle_token_currency: OracleTokenCurrencyService {
                by_key: OracleTokenCurrencyKey::new(Arc::clone(&store)),
                by_id: OracleTokenCurrency::new(Arc::clone(&store)),
            },
            oracle_history: OracleHistoryService {
                by_id: OracleHistory::new(Arc::clone(&store)),
                by_key: OracleHistoryOracleIdSort::new(Arc::clone(&store)),
            },
            price_ticker: PriceTickerService {
                by_id: PriceTicker::new(Arc::clone(&store)),
                by_key: PriceTickerKey::new(Arc::clone(&store)),
            },
            script_activity: ScriptActivityService {
                by_id: ScriptActivity::new(Arc::clone(&store)),
            },
            script_aggregation: ScriptAggregationService {
                by_id: ScriptAggregation::new(Arc::clone(&store)),
            },
            script_unspent: ScriptUnspentService {
                by_id: ScriptUnspent::new(Arc::clone(&store)),
                by_key: ScriptUnspentKey::new(Arc::clone(&store)),
            },
            token_graph: Arc::new(Mutex::new(UnGraphMap::new())),
            store: Arc::clone(&store),
        }
    }
}
