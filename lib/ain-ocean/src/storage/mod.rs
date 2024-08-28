#[macro_use]
mod macros;

mod ocean_store;

use std::sync::Arc;

use crate::{define_table, model, Error, Result};
use ain_db::{Column, ColumnName, DBError, LedgerColumn, Result as DBResult, TypedColumn};
use bitcoin::{hashes::Hash, BlockHash, Txid};
pub use ocean_store::OceanStore;
use rocksdb::Direction;

#[derive(Debug, PartialEq, Clone)]
pub enum SortOrder {
    Ascending,
    Descending,
}

impl From<SortOrder> for Direction {
    fn from(sort_order: SortOrder) -> Self {
        match sort_order {
            SortOrder::Ascending => Direction::Forward,
            SortOrder::Descending => Direction::Reverse,
        }
    }
}

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

define_table! {
    #[derive(Debug)]
    pub struct Block {
        key_type = BlockHash,
        value_type = model::Block,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct BlockByHeight {
        key_type = u32,
        value_type = BlockHash,
        custom_key = {
            fn key(index: &Self::Index) -> DBResult<Vec<u8>> {
                Ok(index.to_be_bytes().to_vec())
            }

            fn get_key(raw_key: Box<[u8]>) -> DBResult<Self::Index> {
                if raw_key.len() != 4 {
                    return Err(DBError::WrongKeyLength);
                }
                let mut array = [0u8; 4];
                array.copy_from_slice(&raw_key);
                Ok(u32::from_be_bytes(array))
            }
        },
    },
    SecondaryIndex = Block
}

define_table! {
    #[derive(Debug)]
    pub struct Masternode {
        key_type = Txid,
        value_type = model::Masternode,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct MasternodeByHeight {
        key_type = (u32, Txid),
        value_type = u8,
    }
}

impl SecondaryIndex<(u32, Txid), u8> for MasternodeByHeight {
    type Value = model::Masternode;

    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
        let ((_, id), _) = el?;
        let col = self.store.column::<Masternode>();
        let tx = col.get(&id)?.ok_or(Error::SecondaryIndex)?;
        Ok(tx)
    }
}

define_table! {
    #[derive(Debug)]
    pub struct MasternodeStats {
        key_type = u32,
        value_type = model::MasternodeStats,
        custom_key = {
            fn key(index: &Self::Index) -> DBResult<Vec<u8>> {
                Ok(index.to_be_bytes().to_vec())
            }

            fn get_key(raw_key: Box<[u8]>) -> DBResult<Self::Index> {
                if raw_key.len() != 4 {
                    return Err(DBError::WrongKeyLength);
                }
                let mut array = [0u8; 4];
                array.copy_from_slice(&raw_key);
                Ok(u32::from_be_bytes(array))
            }
        },
    }
}

impl MasternodeStats {
    pub fn get_latest(&self) -> Result<Option<model::MasternodeStats>> {
        match self.col.iter(None, SortOrder::Descending.into())?.next() {
            None => Ok(None),
            Some(Ok((_, id))) => Ok(Some(id)),
            Some(Err(e)) => Err(e.into()),
        }
    }
}

define_table! {
    #[derive(Debug)]
    pub struct Oracle {
        key_type = Txid,
        value_type = model::Oracle,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct OracleHistory {
        key_type = model::OracleHistoryId,
        value_type = model::OracleHistory,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct OracleHistoryOracleIdSort {
        key_type = Txid,
        value_type = model::OracleHistoryId,
    },
    SecondaryIndex = OracleHistory
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceActive {
        key_type = model::OraclePriceActiveId,
        value_type = model::OraclePriceActive,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceActiveKey {
        key_type = model::OraclePriceActiveKey,
        value_type = model::OraclePriceActiveId,
    },
    SecondaryIndex = OraclePriceActive
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceAggregated {
        key_type = model::OraclePriceAggregatedId,
        value_type = model::OraclePriceAggregated,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceAggregatedKey {
        key_type = model::OraclePriceAggregatedKey,
        value_type = model::OraclePriceAggregatedId,
    },
    SecondaryIndex = OraclePriceAggregated
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceAggregatedInterval {
        key_type = model::OraclePriceAggregatedIntervalId,
        value_type = model::OraclePriceAggregatedInterval,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceAggregatedIntervalKey {
        key_type = model::OraclePriceAggregatedIntervalKey,
        value_type = model::OraclePriceAggregatedIntervalId,
    },
    SecondaryIndex = OraclePriceAggregatedInterval
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceFeed {
        key_type = model::OraclePriceFeedId,
        value_type = model::OraclePriceFeed,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct OraclePriceFeedKey {
        key_type = model::OraclePriceFeedkey,
        value_type = model::OraclePriceFeedId,
    },
    SecondaryIndex = OraclePriceFeed
}

define_table! {
    #[derive(Debug)]
    pub struct OracleTokenCurrency {
        key_type = model::OracleTokenCurrencyId,
        value_type = model::OracleTokenCurrency,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct OracleTokenCurrencyKey {
        key_type = model::OracleTokenCurrencyKey,
        value_type = model::OracleTokenCurrencyId,
    },
    SecondaryIndex = OracleTokenCurrency
}

define_table! {
    #[derive(Debug)]
    pub struct PoolSwap {
        key_type = model::PoolSwapKey,
        value_type = model::PoolSwap,
    },
    InitialKeyProvider = |pk: u32| (pk, u32::MAX, usize::MAX)
}

define_table! {
    #[derive(Debug)]
    pub struct PoolSwapAggregated {
        key_type = model::PoolSwapAggregatedId,
        value_type = model::PoolSwapAggregated,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct PoolSwapAggregatedKey {
        key_type = model::PoolSwapAggregatedKey,
        value_type = model::PoolSwapAggregatedId,
        custom_key = {
            fn key(index: &Self::Index) -> DBResult<Vec<u8>> {
                let (pool_id, interval, bucket) = index;
                let mut vec = Vec::with_capacity(16);
                vec.extend_from_slice(&pool_id.to_be_bytes());
                vec.extend_from_slice(&interval.to_be_bytes());
                vec.extend_from_slice(&bucket.to_be_bytes());
                Ok(vec)
            }

            fn get_key(raw_key: Box<[u8]>) -> DBResult<Self::Index> {
                if raw_key.len() != 16 {
                    return Err(DBError::WrongKeyLength);
                }
                let pool_id = u32::from_be_bytes(
                    raw_key[0..4]
                        .try_into()
                        .map_err(|_| DBError::WrongKeyLength)?,
                );
                let interval = u32::from_be_bytes(
                    raw_key[4..8]
                        .try_into()
                        .map_err(|_| DBError::WrongKeyLength)?,
                );
                let bucket = i64::from_be_bytes(
                    raw_key[8..16]
                        .try_into()
                        .map_err(|_| DBError::WrongKeyLength)?,
                );

                Ok((pool_id, interval, bucket))
                }
            },
    },
    SecondaryIndex = PoolSwapAggregated
}

define_table! {
    #[derive(Debug)]
    pub struct PoolPair {
        key_type = (u32, u32),
        value_type = u32,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct PoolPairByHeight {
        key_type = (u32, usize),
        value_type = (u32, u32, u32),
    }
}

define_table! {
    #[derive(Debug)]
    pub struct PriceTicker {
        key_type = model::PriceTickerId,
        value_type = model::PriceTicker,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct PriceTickerKey {
        key_type = model::PriceTickerKey,
        value_type = model::PriceTickerId,
    },
    SecondaryIndex = PriceTicker
}

define_table! {
    #[derive(Debug)]
    pub struct RawBlock {
        key_type = BlockHash,
        value_type = String,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct ScriptActivity {
        key_type = model::ScriptActivityId,
        value_type = model::ScriptActivity,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct ScriptAggregation {
        key_type = model::ScriptAggregationId,
        value_type = model::ScriptAggregation,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct ScriptUnspent {
        key_type = model::ScriptUnspentId,
        value_type = model::ScriptUnspent,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct ScriptUnspentKey {
        key_type = model::ScriptUnspentKey,
        value_type = model::ScriptUnspentId,
    },
    SecondaryIndex = ScriptUnspent
}

define_table! {
    #[derive(Debug)]
    pub struct Transaction {
        key_type = Txid,
        value_type = model::Transaction,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct TransactionByBlockHash {
        key_type = model::TransactionByBlockHashKey,
        value_type = Txid,
        custom_key = {
            fn key(index: &Self::Index) -> DBResult<Vec<u8>> {
                let (hash, txno) = index;
                let mut vec = hash.as_byte_array().to_vec();
                vec.extend_from_slice(&txno.to_be_bytes());
                Ok(vec)
            }

            fn get_key(raw_key: Box<[u8]>) -> DBResult<Self::Index> {
                if raw_key.len() != 40 {
                    return Err(DBError::WrongKeyLength);
                }
                let mut hash_array = [0u8; 32];
                hash_array.copy_from_slice(&raw_key[..32]);
                let mut txno_array = [0u8; 8];
                txno_array.copy_from_slice(&raw_key[32..]);

                let hash = BlockHash::from_byte_array(hash_array);
                let txno = usize::from_be_bytes(txno_array);
                Ok((hash, txno))
            }
        },
    },
    SecondaryIndex = Transaction,
    InitialKeyProvider = |pk: BlockHash| (pk, 0)
}

define_table! {
    #[derive(Debug)]
    pub struct TransactionVin {
        key_type = String,
        value_type = model::TransactionVin,
    },
    InitialKeyProvider = |pk: Txid| format!("{}00", pk)
}

define_table! {
    #[derive(Debug)]
    pub struct TransactionVout {
        key_type = model::TransactionVoutKey,
        value_type = model::TransactionVout,
        custom_key = {
            fn key(index: &Self::Index) -> DBResult<Vec<u8>> {
                let (txid, txno) = index;
                let mut vec = txid.as_byte_array().to_vec();
                vec.extend_from_slice(&txno.to_be_bytes());
                Ok(vec)
            }

            fn get_key(raw_key: Box<[u8]>) -> DBResult<Self::Index> {
                if raw_key.len() != 40 {
                    return Err(DBError::WrongKeyLength);
                }
                let mut hash_array = [0u8; 32];
                hash_array.copy_from_slice(&raw_key[..32]);
                let mut txno_array = [0u8; 8];
                txno_array.copy_from_slice(&raw_key[32..]);

                let txid = Txid::from_byte_array(hash_array);
                let txno = usize::from_be_bytes(txno_array);
                Ok((txid, txno))
            }
        },
    }
}

define_table! {
    #[derive(Debug)]
    pub struct TxResult {
        key_type = Txid,
        value_type = model::TxResult,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct VaultAuctionHistory {
        key_type = model::AuctionHistoryKey,
        value_type = model::VaultAuctionBatchHistory,
    }
}

define_table! {
    #[derive(Debug)]
    pub struct VaultAuctionHistoryByHeight {
        key_type = model::AuctionHistoryByHeightKey,
        value_type = model::AuctionHistoryKey,
    },
    SecondaryIndex = VaultAuctionHistory
}

pub const COLUMN_NAMES: [&str; 37] = [
    Block::NAME,
    BlockByHeight::NAME,
    MasternodeStats::NAME,
    Masternode::NAME,
    MasternodeByHeight::NAME,
    Oracle::NAME,
    OracleHistory::NAME,
    OracleHistoryOracleIdSort::NAME,
    OraclePriceActive::NAME,
    OraclePriceActiveKey::NAME,
    OraclePriceAggregated::NAME,
    OraclePriceAggregatedKey::NAME,
    OraclePriceAggregatedInterval::NAME,
    OraclePriceAggregatedIntervalKey::NAME,
    OraclePriceFeed::NAME,
    OraclePriceFeedKey::NAME,
    OracleTokenCurrency::NAME,
    OracleTokenCurrencyKey::NAME,
    PoolSwapAggregated::NAME,
    PoolSwapAggregatedKey::NAME,
    PoolSwap::NAME,
    PoolPair::NAME,
    PoolPairByHeight::NAME,
    PriceTicker::NAME,
    PriceTickerKey::NAME,
    RawBlock::NAME,
    ScriptActivity::NAME,
    ScriptAggregation::NAME,
    ScriptUnspent::NAME,
    ScriptUnspentKey::NAME,
    Transaction::NAME,
    TransactionByBlockHash::NAME,
    TransactionVin::NAME,
    TransactionVout::NAME,
    TxResult::NAME,
    VaultAuctionHistory::NAME,
    VaultAuctionHistoryByHeight::NAME,
];
