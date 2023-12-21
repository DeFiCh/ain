use ain_db::{ColumnName, TypedColumn};
use rocksdb::{DBIterator, IteratorMode};

use crate::Result;

pub mod block;
pub mod masternode;
pub mod masternode_stats;
pub mod oracle;
pub mod oracle_history;
pub mod oracle_price_active;
pub mod oracle_price_aggregated;
pub mod oracle_price_aggregated_interval;
pub mod oracle_price_feed;
pub mod oracle_token_currency;
pub mod poolswap;
pub mod poolswap_aggregated;
pub mod price_ticker;
pub mod raw_block;
pub mod script_activity;
pub mod script_aggregation;
pub mod script_unspent;
pub mod transaction;
pub mod transaction_vin;
pub mod transaction_vout;
pub mod vault_auction_batch_history;
