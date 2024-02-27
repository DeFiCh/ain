use crate::{storage::SortOrder, Result};

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
mod tx_result;
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
pub use tx_result::*;
pub use vault_auction_batch_history::*;

pub trait RepositoryOps<K, V> {
    type ListItem;
    fn get(&self, key: &K) -> Result<Option<V>>;
    fn put(&self, key: &K, value: &V) -> Result<()>;
    fn delete(&self, key: &K) -> Result<()>;
    fn list<'a>(
        &'a self,
        from: Option<K>,
        direction: SortOrder,
    ) -> Result<Box<dyn Iterator<Item = Self::ListItem> + 'a>>;
}

pub trait InitialKeyProvider<K, V>: RepositoryOps<K, V> {
    type PartialKey;
    fn initial_key(pk: Self::PartialKey) -> K;
}

pub trait SecondaryIndex<K, V>: RepositoryOps<K, V> {
    type Value;
    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value>;
}
