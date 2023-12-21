use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};
use rocksdb::IteratorMode;

use crate::model::{masternode::Masternode, masternode_stats::MasternodeStats};

#[derive(Debug, PartialEq, Clone)]
pub enum SortOrder {
    Ascending,
    Descending,
}

impl<'a> From<SortOrder> for IteratorMode<'a> {
    fn from(sort_order: SortOrder) -> Self {
        match sort_order {
            SortOrder::Ascending => IteratorMode::Start,
            SortOrder::Descending => IteratorMode::End,
        }
    }
}

pub mod columns {
    #[derive(Debug)]
    pub struct Block;

    #[derive(Debug)]
    pub struct MasternodeStats;

    #[derive(Debug)]
    pub struct Masternode;
    #[derive(Debug)]
    pub struct OracleHistory;

    #[derive(Debug)]
    pub struct OraclePriceActive;

    #[derive(Debug)]
    pub struct OraclePriveAggregated;
    #[derive(Debug)]
    pub struct OraclePriveAggregatedInterval;
    #[derive(Debug)]
    pub struct OraclePriveFeed;
    #[derive(Debug)]
    pub struct OracleTokenCurrency;
    #[derive(Debug)]
    pub struct PoolSwapAggregated;
    #[derive(Debug)]
    pub struct PoolSwap;
    #[derive(Debug)]
    pub struct PriceTicker;
    #[derive(Debug)]
    pub struct RawBlock;
    #[derive(Debug)]
    pub struct ScriptActivity;
    #[derive(Debug)]
    pub struct ScriptAggregation;
    #[derive(Debug)]
    pub struct ScriptUnspent;
    #[derive(Debug)]
    pub struct Transaction;
    #[derive(Debug)]
    pub struct TransactionVin;
    #[derive(Debug)]
    pub struct TransactionVout;
    #[derive(Debug)]
    pub struct VaultAuctionHistory;
}

//
// ColumnName impl
//

impl ColumnName for columns::Block {
    const NAME: &'static str = "block";
}

impl ColumnName for columns::MasternodeStats {
    const NAME: &'static str = "masternode_stats";
}

impl ColumnName for columns::Masternode {
    const NAME: &'static str = "masternode";
}
impl ColumnName for columns::OracleHistory {
    const NAME: &'static str = "oracle_history";
}

impl ColumnName for columns::OraclePriceActive {
    const NAME: &'static str = "oracle_price_active";
}

impl ColumnName for columns::OraclePriveAggregated {
    const NAME: &'static str = "oracle_price_aggregated";
}
impl ColumnName for columns::OraclePriveAggregatedInterval {
    const NAME: &'static str = "oracle_price_aggregated_interval";
}
impl ColumnName for columns::OraclePriveFeed {
    const NAME: &'static str = "oracle_price_feed";
}
impl ColumnName for columns::OracleTokenCurrency {
    const NAME: &'static str = "oracle_token_currency";
}
impl ColumnName for columns::PoolSwapAggregated {
    const NAME: &'static str = "pool_swap_aggregated";
}
impl ColumnName for columns::PoolSwap {
    const NAME: &'static str = "pool_swap";
}
impl ColumnName for columns::PriceTicker {
    const NAME: &'static str = "price_tiker";
}
impl ColumnName for columns::RawBlock {
    const NAME: &'static str = "raw_block";
}
impl ColumnName for columns::ScriptActivity {
    const NAME: &'static str = "script_activity";
}
impl ColumnName for columns::ScriptAggregation {
    const NAME: &'static str = "script_aggregation";
}
impl ColumnName for columns::ScriptUnspent {
    const NAME: &'static str = "script_unspent";
}
impl ColumnName for columns::Transaction {
    const NAME: &'static str = "transaction";
}
impl ColumnName for columns::TransactionVin {
    const NAME: &'static str = "transaction_vin";
}
impl ColumnName for columns::TransactionVout {
    const NAME: &'static str = "transaction_vout";
}
impl ColumnName for columns::VaultAuctionHistory {
    const NAME: &'static str = "vault_auction_history";
}

pub const COLUMN_NAMES: [&'static str; 20] = [
    columns::Block::NAME,
    columns::MasternodeStats::NAME,
    columns::Masternode::NAME,
    columns::OracleHistory::NAME,
    columns::OraclePriceActive::NAME,
    columns::OraclePriveAggregated::NAME,
    columns::OraclePriveAggregatedInterval::NAME,
    columns::OraclePriveFeed::NAME,
    columns::OracleTokenCurrency::NAME,
    columns::PoolSwapAggregated::NAME,
    columns::PoolSwap::NAME,
    columns::PriceTicker::NAME,
    columns::RawBlock::NAME,
    columns::ScriptActivity::NAME,
    columns::ScriptAggregation::NAME,
    columns::ScriptUnspent::NAME,
    columns::Transaction::NAME,
    columns::TransactionVin::NAME,
    columns::TransactionVout::NAME,
    columns::VaultAuctionHistory::NAME,
];

//
// Column trait impl
//

impl Column for columns::Block {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::MasternodeStats {
    type Index = Txid;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_byte_array().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Self::Index::from_slice(&raw_key)
            .map_err(|_| DBError::Custom(format_err!("Error parsing key")))
    }
}

impl Column for columns::Masternode {
    type Index = Txid;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_byte_array().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Self::Index::from_slice(&raw_key)
            .map_err(|_| DBError::Custom(format_err!("Error parsing key")))
    }
}

impl Column for columns::OracleHistory {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::OraclePriceActive {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::OraclePriveAggregated {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::OraclePriveAggregatedInterval {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::OraclePriveFeed {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::OracleTokenCurrency {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::PoolSwapAggregated {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::PoolSwap {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::PriceTicker {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::RawBlock {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::ScriptActivity {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::ScriptAggregation {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::ScriptUnspent {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::Transaction {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::TransactionVin {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::TransactionVout {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl Column for columns::VaultAuctionHistory {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

//
// TypedColumn impl
//
impl TypedColumn for columns::Block {
    type Type = String;
}

impl TypedColumn for columns::MasternodeStats {
    type Type = MasternodeStats;
}

impl TypedColumn for columns::Masternode {
    type Type = Masternode;
}

impl TypedColumn for columns::OracleHistory {
    type Type = String;
}

impl TypedColumn for columns::OraclePriceActive {
    type Type = String;
}

impl TypedColumn for columns::OraclePriveAggregated {
    type Type = String;
}

impl TypedColumn for columns::OraclePriveAggregatedInterval {
    type Type = String;
}

impl TypedColumn for columns::OraclePriveFeed {
    type Type = String;
}

impl TypedColumn for columns::OracleTokenCurrency {
    type Type = String;
}

impl TypedColumn for columns::PoolSwapAggregated {
    type Type = String;
}

impl TypedColumn for columns::PoolSwap {
    type Type = String;
}

impl TypedColumn for columns::PriceTicker {
    type Type = String;
}

impl TypedColumn for columns::RawBlock {
    type Type = String;
}

impl TypedColumn for columns::ScriptActivity {
    type Type = String;
}

impl TypedColumn for columns::ScriptAggregation {
    type Type = String;
}

impl TypedColumn for columns::ScriptUnspent {
    type Type = String;
}

impl TypedColumn for columns::Transaction {
    type Type = String;
}

impl TypedColumn for columns::TransactionVin {
    type Type = String;
}

impl TypedColumn for columns::TransactionVout {
    type Type = String;
}

impl TypedColumn for columns::VaultAuctionHistory {
    type Type = String;
}
