use crate::Result;

pub mod block;
pub mod masternode;
pub mod masternode_stats;
pub mod oracle_price_active;
pub mod oracle_price_aggregated;
pub mod oracle_price_aggregated_interval;
pub mod oracle_price_feed;
pub mod oracle_token_currency;
pub mod pool_swap;
pub mod pool_swap_aggregated;
pub mod price_ticker;
pub mod raw_block;
pub mod script_activity;
pub mod script_aggregation;
pub mod script_unspent;
pub mod test;
pub mod transaction;
pub mod transaction_vin;
pub mod transaction_vout;
pub mod vault_auction_batch_history;

trait RepositoryOps<K, V> {
    fn get(&self, key: K) -> Result<Option<V>>;
    fn put(&self, key: &K, masternode: &V) -> Result<()>;
    fn delete(&self, key: &K) -> Result<()>;
    fn list(&self, from: Option<K>, limit: usize) -> Result<Vec<(K, V)>>;
}
