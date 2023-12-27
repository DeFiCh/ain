use crate::Result;

mod block;
mod masternode;
mod masternode_stats;
mod oracle_price_active;
mod oracle_price_aggregated;
mod oracle_price_aggregated_interval;
mod oracle_price_feed;
mod oracle_token_currency;
mod pool_swap;
mod pool_swap_aggregated;
mod price_ticker;
mod raw_block;
mod script_activity;
mod script_aggregation;
mod script_unspent;
mod test;
mod transaction;
mod transaction_vin;
mod transaction_vout;
mod vault_auction_batch_history;

pub use block::*;
pub use masternode::*;
pub use masternode_stats::*;
pub use oracle_price_active::*;
pub use oracle_price_aggregated::*;
pub use oracle_price_aggregated_interval::*;
pub use oracle_price_feed::*;
pub use oracle_token_currency::*;
pub use pool_swap::*;
pub use pool_swap_aggregated::*;
pub use price_ticker::*;
pub use raw_block::*;
pub use script_activity::*;
pub use script_aggregation::*;
pub use script_unspent::*;
pub use test::*;
pub use transaction::*;
pub use transaction_vin::*;
pub use transaction_vout::*;
pub use vault_auction_batch_history::*;

pub trait RepositoryOps<K, V> {
    fn get(&self, key: K) -> Result<Option<V>>;
    fn put(&self, key: &K, masternode: &V) -> Result<()>;
    fn delete(&self, key: &K) -> Result<()>;
    fn list(&self, from: Option<K>, limit: usize) -> Result<Vec<(K, V)>>;
}
