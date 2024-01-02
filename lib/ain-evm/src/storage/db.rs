use std::{
    collections::HashMap,
    fmt::Debug,
    iter::Iterator,
    marker::PhantomData,
    path::{Path, PathBuf},
    sync::Arc,
};

use ain_db::{Column, ColumnName, DBError, LedgerColumn, TypedColumn};
use bincode;
use ethereum::{BlockAny, TransactionV2};
use ethereum_types::{H160, H256, U256};
use rocksdb::{
    BlockBasedOptions, Cache, ColumnFamily, ColumnFamilyDescriptor, DBIterator, IteratorMode,
    Options, DB,
};
use serde::{de::DeserializeOwned, Serialize};

use crate::{log::LogIndex, receipt::Receipt};

pub mod columns {

    #[derive(Debug)]
    /// Column family for blocks data
    pub struct Blocks;

    #[derive(Debug)]
    /// Column family for transactions data
    pub struct Transactions;

    #[derive(Debug)]
    /// Column family for receipts data
    pub struct Receipts;

    #[derive(Debug)]
    /// Column family for block map data
    pub struct BlockMap;

    #[derive(Debug)]
    /// Column family for latest block number data
    pub struct LatestBlockNumber;

    #[derive(Debug)]
    /// Column family for address logs map data
    pub struct AddressLogsMap;

    #[derive(Debug)]
    /// Column family for code map data
    pub struct AddressCodeMap;

    #[derive(Debug)]
    /// Column family for block code map data
    pub struct BlockDeployedCodeHashes;
}

const BLOCKS_CF: &str = "blocks";
const TRANSACTIONS_CF: &str = "transactions";
const RECEIPTS_CF: &str = "receipts";
const BLOCK_MAP_CF: &str = "block_map";
const LATEST_BLOCK_NUMBER_CF: &str = "latest_block_number";
const ADDRESS_LOGS_MAP_CF: &str = "address_logs_map";
const ADDRESS_CODE_MAP_CF: &str = "address_code_map";
const BLOCK_DEPLOYED_CODES_CF: &str = "block_deployed_codes";

//
// ColumnName impl
//
impl ColumnName for columns::Transactions {
    const NAME: &'static str = TRANSACTIONS_CF;
}

impl ColumnName for columns::Blocks {
    const NAME: &'static str = BLOCKS_CF;
}

impl ColumnName for columns::Receipts {
    const NAME: &'static str = RECEIPTS_CF;
}

impl ColumnName for columns::BlockMap {
    const NAME: &'static str = BLOCK_MAP_CF;
}

impl ColumnName for columns::LatestBlockNumber {
    const NAME: &'static str = LATEST_BLOCK_NUMBER_CF;
}

impl ColumnName for columns::AddressLogsMap {
    const NAME: &'static str = ADDRESS_LOGS_MAP_CF;
}

impl ColumnName for columns::AddressCodeMap {
    const NAME: &'static str = ADDRESS_CODE_MAP_CF;
}

impl ColumnName for columns::BlockDeployedCodeHashes {
    const NAME: &'static str = BLOCK_DEPLOYED_CODES_CF;
}

pub const COLUMN_NAMES: [&'static str; 8] = [
    columns::Blocks::NAME,
    columns::Transactions::NAME,
    columns::Receipts::NAME,
    columns::BlockMap::NAME,
    columns::LatestBlockNumber::NAME,
    columns::AddressLogsMap::NAME,
    columns::AddressCodeMap::NAME,
    columns::BlockDeployedCodeHashes::NAME,
];

//
// Column trait impl
//

impl Column for columns::Transactions {
    type Index = H256;
}

impl Column for columns::Blocks {
    type Index = U256;
}

impl Column for columns::Receipts {
    type Index = H256;
}

impl Column for columns::BlockMap {
    type Index = H256;
}

impl Column for columns::LatestBlockNumber {
    type Index = String;

    fn key(_index: &Self::Index) -> Result<Vec<u8>, DBError> {
        Ok(b"latest".to_vec())
    }

    fn get_key(_raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Ok(String::from("latest"))
    }
}

impl Column for columns::AddressLogsMap {
    type Index = U256;
}

impl Column for columns::AddressCodeMap {
    type Index = (H160, H256);
}

impl Column for columns::BlockDeployedCodeHashes {
    type Index = (U256, H160);
}

//
// TypedColumn impl
//
impl TypedColumn for columns::Transactions {
    type Type = TransactionV2;
}

impl TypedColumn for columns::Blocks {
    type Type = BlockAny;
}

impl TypedColumn for columns::Receipts {
    type Type = Receipt;
}

impl TypedColumn for columns::BlockMap {
    type Type = U256;
}

impl TypedColumn for columns::LatestBlockNumber {
    type Type = U256;
}

impl TypedColumn for columns::AddressLogsMap {
    type Type = HashMap<H160, Vec<LogIndex>>;
}

impl TypedColumn for columns::AddressCodeMap {
    type Type = Vec<u8>;
}

impl TypedColumn for columns::BlockDeployedCodeHashes {
    type Type = H256;
}
