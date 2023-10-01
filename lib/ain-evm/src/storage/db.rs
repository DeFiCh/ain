use std::{
    collections::HashMap,
    marker::PhantomData,
    path::{Path, PathBuf},
    sync::Arc,
};

use bincode;
use ethereum::{BlockAny, TransactionV2};
use ethereum_types::{H160, H256, U256};
use rocksdb::{BlockBasedOptions, Cache, ColumnFamily, ColumnFamilyDescriptor, Options, DB};
use serde::{de::DeserializeOwned, Serialize};

use crate::{log::LogIndex, receipt::Receipt, Result};

fn get_db_options() -> Options {
    let mut options = Options::default();
    options.create_if_missing(true);
    options.create_missing_column_families(true);
    // A good value for this is the number of cores on the machine // TODO fetch via ffi
    options.increase_parallelism(2);

    let mut env = rocksdb::Env::new().unwrap();

    // While a compaction is ongoing, all the background threads
    // could be used by the compaction. This can stall writes which
    // need to flush the memtable. Add some high-priority background threads
    // which can service these writes.
    env.set_high_priority_background_threads(4);
    options.set_env(&env);

    // Set max total wal size to 4G.
    options.set_max_total_wal_size(4 * 1024 * 1024 * 1024);

    let cache = Cache::new_lru_cache(512 * 1024 * 1024);
    let mut block_opts = BlockBasedOptions::default();
    block_opts.set_block_cache(&cache);
    block_opts.set_bloom_filter(10.0, false);
    options.set_block_based_table_factory(&block_opts);

    options
}

#[derive(Debug)]
pub struct Rocks(DB);

impl Rocks {
    pub fn open(path: &PathBuf) -> Result<Self> {
        let cf_descriptors = Self::column_names()
            .into_iter()
            .map(|cf_name| ColumnFamilyDescriptor::new(cf_name, Options::default()));

        let db_opts = get_db_options();
        let db = DB::open_cf_descriptors(&db_opts, path, cf_descriptors)?;

        Ok(Self(db))
    }

    fn column_names() -> Vec<&'static str> {
        vec![
            columns::Blocks::NAME,
            columns::Transactions::NAME,
            columns::Receipts::NAME,
            columns::BlockMap::NAME,
            columns::LatestBlockNumber::NAME,
            columns::CodeMap::NAME,
            columns::AddressLogsMap::NAME,
        ]
    }

    #[allow(dead_code)]
    fn destroy(path: &Path) -> Result<()> {
        DB::destroy(&Options::default(), path)?;

        Ok(())
    }

    pub fn cf_handle(&self, cf: &str) -> &ColumnFamily {
        self.0
            .cf_handle(cf)
            .expect("should never get an unknown column")
    }

    fn get_cf(&self, cf: &ColumnFamily, key: &[u8]) -> Result<Option<Vec<u8>>> {
        let opt = self.0.get_cf(cf, key)?;
        Ok(opt)
    }

    fn put_cf(&self, cf: &ColumnFamily, key: &[u8], value: &[u8]) -> Result<()> {
        self.0.put_cf(cf, key, value)?;
        Ok(())
    }

    fn delete_cf(&self, cf: &ColumnFamily, key: &[u8]) -> Result<()> {
        self.0.delete_cf(cf, key)?;
        Ok(())
    }

    pub fn flush(&self) -> Result<()> {
        self.0.flush()?;
        Ok(())
    }
}

#[derive(Debug, Clone)]
pub struct LedgerColumn<C>
where
    C: Column + ColumnName,
{
    pub backend: Arc<Rocks>,
    pub column: PhantomData<C>,
}

impl<C> LedgerColumn<C>
where
    C: Column + ColumnName,
{
    pub fn get_bytes(&self, key: &C::Index) -> Result<Option<Vec<u8>>> {
        self.backend.get_cf(self.handle(), &C::key(key))
    }

    pub fn put_bytes(&self, key: &C::Index, value: &[u8]) -> Result<()> {
        self.backend.put_cf(self.handle(), &C::key(key), value)
    }

    pub fn handle(&self) -> &ColumnFamily {
        self.backend.cf_handle(C::NAME)
    }
}

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
    /// Column family for code map data
    pub struct CodeMap;

    #[derive(Debug)]
    /// Column family for address logs map data
    pub struct AddressLogsMap;
}

//
// ColumnName trait. Define associated column family NAME
//
pub trait ColumnName {
    const NAME: &'static str;
}

const BLOCKS_CF: &str = "blocks";
const TRANSACTIONS_CF: &str = "transactions";
const RECEIPTS_CF: &str = "receipts";
const BLOCK_MAP_CF: &str = "block_map";
const LATEST_BLOCK_NUMBER_CF: &str = "latest_block_number";
const CODE_MAP_CF: &str = "code_map";
const ADDRESS_LOGS_MAP_CF: &str = "address_logs_map";

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
impl ColumnName for columns::CodeMap {
    const NAME: &'static str = CODE_MAP_CF;
}

//
// Column trait. Define associated index type
//
pub trait Column {
    type Index;

    fn key(index: &Self::Index) -> Vec<u8>;
}

//
// Column trait impl
//

impl Column for columns::Transactions {
    type Index = H256;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }
}

impl Column for columns::Blocks {
    type Index = U256;

    fn key(index: &Self::Index) -> Vec<u8> {
        let mut bytes = [0_u8; 32];
        index.to_big_endian(&mut bytes);
        bytes.to_vec()
    }
}

impl Column for columns::Receipts {
    type Index = H256;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.to_fixed_bytes().to_vec()
    }
}
impl Column for columns::BlockMap {
    type Index = H256;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.to_fixed_bytes().to_vec()
    }
}

impl Column for columns::LatestBlockNumber {
    type Index = ();

    fn key(_index: &Self::Index) -> Vec<u8> {
        b"latest".to_vec()
    }
}

impl Column for columns::AddressLogsMap {
    type Index = U256;

    fn key(index: &Self::Index) -> Vec<u8> {
        let mut bytes = [0_u8; 32];
        index.to_big_endian(&mut bytes);
        bytes.to_vec()
    }
}

impl Column for columns::CodeMap {
    type Index = H256;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.to_fixed_bytes().to_vec()
    }
}

//
// TypedColumn trait. Define associated value type
//
pub trait TypedColumn: Column {
    type Type: Serialize + DeserializeOwned;
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

impl<C> LedgerColumn<C>
where
    C: TypedColumn + ColumnName,
{
    pub fn get(&self, key: &C::Index) -> Result<Option<C::Type>> {
        if let Some(serialized_value) = self.get_bytes(key)? {
            let value = bincode::deserialize(&serialized_value)?;

            Ok(Some(value))
        } else {
            Ok(None)
        }
    }

    pub fn put(&self, key: &C::Index, value: &C::Type) -> Result<()> {
        let serialized_value = bincode::serialize(value)?;

        self.put_bytes(key, &serialized_value)
    }

    pub fn delete(&self, key: &C::Index) -> Result<()> {
        self.backend.delete_cf(self.handle(), &C::key(key))
    }
}
