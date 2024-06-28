mod block;
mod masternode;
mod masternode_stats;
mod oracle;
mod oracle_history;
mod oracle_price_active;
mod oracle_price_aggregated;
mod oracle_price_aggregated_interval;
mod oracle_price_feed;
mod oracle_token_currency;
mod pool_swap;
mod pool_swap_aggregated;
mod poolpair;
mod price_ticker;
mod raw_block;
mod script_activity;
mod script_aggregation;
mod script_unspent;
mod transaction;
mod transaction_vin;
mod transaction_vout;
mod tx_result;
mod vault_auction_history;

use ain_db::ColumnName;
pub use block::*;
pub use masternode::*;
pub use masternode_stats::*;
pub use oracle::*;
pub use oracle_history::*;
pub use oracle_price_active::*;
pub use oracle_price_aggregated::*;
pub use oracle_price_aggregated_interval::*;
pub use oracle_price_feed::*;
pub use oracle_token_currency::*;
pub use pool_swap::*;
pub use pool_swap_aggregated::*;
pub use poolpair::*;
pub use price_ticker::*;
pub use raw_block::*;
pub use script_activity::*;
pub use script_aggregation::*;
pub use script_unspent::*;
pub use transaction::*;
pub use transaction_vin::*;
pub use transaction_vout::*;
pub use tx_result::*;
pub use vault_auction_history::*;

pub const COLUMN_NAMES: [&str; 38] = [
    block::Block::NAME,
    block::BlockByHeight::NAME,
    masternode_stats::MasternodeStats::NAME,
    masternode::Masternode::NAME,
    masternode::MasternodeByHeight::NAME,
    oracle::Oracle::NAME,
    oracle_history::OracleHistory::NAME,
    oracle_history::OracleHistoryOracleIdSort::NAME,
    oracle_price_active::OraclePriceActive::NAME,
    oracle_price_active::OraclePriceActiveKey::NAME,
    oracle_price_aggregated::OraclePriceAggregated::NAME,
    oracle_price_aggregated::OraclePriceAggregatedKey::NAME,
    oracle_price_aggregated_interval::OraclePriceAggregatedInterval::NAME,
    oracle_price_aggregated_interval::OraclePriceAggregatedIntervalKey::NAME,
    oracle_price_feed::OraclePriceFeed::NAME,
    oracle_price_feed::OraclePriceFeedKey::NAME,
    oracle_token_currency::OracleTokenCurrency::NAME,
    oracle_token_currency::OracleTokenCurrencyKey::NAME,
    pool_swap_aggregated::PoolSwapAggregated::NAME,
    pool_swap_aggregated::PoolSwapAggregatedKey::NAME,
    pool_swap::PoolSwap::NAME,
    poolpair::PoolPair::NAME,
    poolpair::PoolPairByHeight::NAME,
    price_ticker::PriceTicker::NAME,
    price_ticker::PriceTickerKey::NAME,
    raw_block::RawBlock::NAME,
    script_activity::ScriptActivity::NAME,
    script_activity::ScriptActivityKey::NAME,
    script_aggregation::ScriptAggregation::NAME,
    script_unspent::ScriptUnspent::NAME,
    script_unspent::ScriptUnspentKey::NAME,
    transaction::Transaction::NAME,
    transaction::TransactionByBlockHash::NAME,
    transaction_vin::TransactionVin::NAME,
    transaction_vout::TransactionVout::NAME,
    tx_result::TxResult::NAME,
    vault_auction_history::VaultAuctionHistory::NAME,
    vault_auction_history::VaultAuctionHistoryByHeight::NAME,
];
