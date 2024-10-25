use std::{
    collections::BTreeMap,
    fmt::Debug,
    iter::Iterator,
    marker::PhantomData,
    path::{Path, PathBuf},
    sync::Arc,
};
pub mod version;
use anyhow::format_err;
use log::debug;
use rocksdb::{
    BlockBasedOptions, Cache, ColumnFamily, ColumnFamilyDescriptor, DBCompressionType, DBIterator,
    Direction, IteratorMode, Options, DB,
};
use serde::{de::DeserializeOwned, Serialize};
use sha2::{Digest, Sha256};

pub type Result<T> = result::Result<T, DBError>;

fn get_db_default_options() -> Options {
    let mut block_opts = BlockBasedOptions::default();
    block_opts.set_block_size(64 << 10); // kb
    block_opts.set_block_cache(&Cache::new_lru_cache(64 << 20)); // mb
    block_opts.set_cache_index_and_filter_blocks(true);
    block_opts.set_bloom_filter(10.0, true);
    // block_opts.set_pin_top_level_index_and_filter(true);

    let mut opts = Options::default();
    opts.create_if_missing(true);
    opts.create_missing_column_families(true);
    opts.set_write_buffer_size(64 << 20); // mb
    opts.set_max_write_buffer_number(2);
    // opts.set_min_blob_size(2 << 10); // kb
    //                                  // opts.set_blob_file_size(256 << 20); // mb
    // opts.set_enable_blob_files(true);
    // opts.set_enable_blob_gc(true);
    opts.set_enable_pipelined_write(true);

    opts.set_compression_type(DBCompressionType::Lz4);
    // opts.set_blob_compression_type(DBCompressionType::Lz4);
    opts.set_bottommost_compression_type(DBCompressionType::Zstd);
    opts.set_block_based_table_factory(&block_opts);
    opts.enable_statistics();
    let n = num_cpus::get();
    opts.increase_parallelism(n as i32);
    opts.set_level_compaction_dynamic_level_bytes(true);
    opts
}

#[derive(Debug)]
pub struct Rocks(DB);

impl Rocks {
    pub fn open(
        path: &PathBuf,
        cf_names: Vec<(&'static str, Option<Options>)>,
        opts: Option<Options>,
    ) -> Result<Self> {
        let cf_descriptors = cf_names.into_iter().map(|(cf_name, opts)| {
            ColumnFamilyDescriptor::new(cf_name, opts.unwrap_or_else(Options::default))
        });

        let db_opts = opts.unwrap_or_else(get_db_default_options);
        let db = DB::open_cf_descriptors(&db_opts, path, cf_descriptors)?;

        Ok(Self(db))
    }

    #[allow(dead_code)]
    fn destroy(path: &Path) -> Result<()> {
        DB::destroy(&Options::default(), path)?;

        Ok(())
    }

    pub fn compact(&self) {
        self.0.compact_range(None::<&[u8]>, None::<&[u8]>);
    }

    pub fn cf_handle(&self, cf: &str) -> Result<&ColumnFamily> {
        self.0
            .cf_handle(cf)
            .ok_or_else(|| DBError::Custom(format_err!("Unknown column: {}", cf)))
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

    pub fn iterator_cf<C>(&self, cf: &ColumnFamily, iterator_mode: IteratorMode) -> DBIterator
    where
        C: Column,
    {
        self.0.iterator_cf(cf, iterator_mode)
    }

    pub fn flush(&self) -> Result<()> {
        self.0.flush()?;
        Ok(())
    }

    pub fn dump_table_sizes(&self, cf_names: &[&'static str]) -> Result<()> {
        let mut stats: BTreeMap<String, (u64, u64, f64)> = BTreeMap::new(); // (size, entries, avg_size)
        let mut total_size: u64 = 0;
        let mut total_entries: u64 = 0;

        for cf_name in cf_names.iter() {
            if let Some(cf) = self.0.cf_handle(cf_name) {
                let size = self
                    .0
                    .property_int_value_cf(cf, "rocksdb.estimate-live-data-size")?
                    .unwrap_or(0);
                let entries = self
                    .0
                    .property_int_value_cf(cf, "rocksdb.estimate-num-keys")?
                    .unwrap_or(0);
                let avg_size = if entries > 0 {
                    size as f64 / entries as f64
                } else {
                    0.0
                };

                stats.insert(cf_name.to_string(), (size, entries, avg_size));
                total_size += size;
                total_entries += entries;
            }
        }

        debug!("RocksDB Table Statistics:");
        debug!("{:-<80}", "");
        debug!(
            "{:<30} {:>10} {:>15} {:>15} {:>10}",
            "Table Name", "Size (MB)", "Entries", "Avg Size (B)", "%% of Total"
        );
        debug!("{:-<80}", "");

        for (name, (size, entries, avg_size)) in stats.iter() {
            let size_mb = *size as f64 / (1024.0 * 1024.0);
            let percentage = (*size as f64 / total_size as f64) * 100.0;
            debug!(
                "{:<30} {:>10.2} {:>15} {:>15.2} {:>9.2}%%",
                name, size_mb, entries, avg_size, percentage
            );
        }

        debug!("{:-<80}", "");
        let total_avg_size = if total_entries > 0 {
            total_size as f64 / total_entries as f64
        } else {
            0.0
        };
        debug!(
            "Total size: {:.2} MB",
            total_size as f64 / (1024.0 * 1024.0)
        );
        debug!("Total entries: {}", total_entries);
        debug!("Overall average entry size: {:.2} bytes", total_avg_size);

        Ok(())
    }

    pub fn hash_db_state(&self, cf_names: &[&'static str]) -> Result<String> {
        let mut hasher = Sha256::new();

        for cf_name in cf_names {
            let cf = self.cf_handle(cf_name)?;
            let iter = self.0.iterator_cf(cf, rocksdb::IteratorMode::Start);

            for item in iter {
                let (key, value) = item?;
                hasher.update(&key);
                hasher.update(&value);
            }
        }

        let result = hasher.finalize();
        Ok(hex::encode(result))
    }
}

//
// ColumnName trait. Define associated column family NAME
//
pub trait ColumnName {
    const NAME: &'static str;
}

//
// Column trait. Define associated index type
//
pub trait Column {
    type Index: Debug + Serialize + DeserializeOwned;

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        bincode::serialize(index).map_err(DBError::Bincode)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        bincode::deserialize(&raw_key).map_err(DBError::Bincode)
    }
}

//
// TypedColumn trait. Define associated value type
//
pub trait TypedColumn: Column {
    type Type: Serialize + DeserializeOwned + Debug;
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
        self.backend.get_cf(self.handle()?, &C::key(key)?)
    }

    pub fn put_bytes(&self, key: &C::Index, value: &[u8]) -> Result<()> {
        self.backend.put_cf(self.handle()?, &C::key(key)?, value)
    }

    pub fn handle(&self) -> Result<&ColumnFamily> {
        self.backend.cf_handle(C::NAME)
    }
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
        self.backend.delete_cf(self.handle()?, &C::key(key)?)
    }

    pub fn iter(
        &self,
        from: Option<C::Index>,
        direction: Direction,
    ) -> Result<impl Iterator<Item = Result<(C::Index, C::Type)>> + '_> {
        let index = from
            .as_ref()
            .map(|i| C::key(i))
            .transpose()?
            .unwrap_or_default();

        let iterator_mode = match direction {
            Direction::Forward => from.map_or(IteratorMode::Start, |_| {
                IteratorMode::From(&index, Direction::Forward)
            }),
            Direction::Reverse => from.map_or(IteratorMode::End, |_| {
                IteratorMode::From(&index, Direction::Reverse)
            }),
        };
        Ok(self
            .backend
            .iterator_cf::<C>(self.handle()?, iterator_mode)
            .map(|k| {
                let (key, value) = k?;
                let value = bincode::deserialize(&value)?;
                let key = C::get_key(key)?;
                Ok((key, value))
            }))
    }
}

use std::{error::Error, fmt, result};

use bincode::Error as BincodeError;
use rocksdb::Error as RocksDBError;

#[derive(Debug)]
pub enum DBError {
    RocksDBError(RocksDBError),
    Bincode(BincodeError),
    ParseKey,
    WrongKeyLength,
    Custom(anyhow::Error),
    UnsupportedVersion,
}

impl fmt::Display for DBError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DBError::RocksDBError(e) => write!(f, "RocksDB Error: {e}"),
            DBError::Bincode(e) => write!(f, "Bincode Error: {e}"),
            DBError::ParseKey => write!(f, "Error parsing key"),
            DBError::WrongKeyLength => write!(f, "Wrong key length"),
            DBError::Custom(e) => write!(f, "Custom Error: {e}"),
            DBError::UnsupportedVersion => write!(f, "DB version higher than expected. Node should be updated to support new DB version."),
        }
    }
}

impl Error for DBError {}

impl From<RocksDBError> for DBError {
    fn from(e: RocksDBError) -> Self {
        DBError::RocksDBError(e)
    }
}

impl From<BincodeError> for DBError {
    fn from(e: BincodeError) -> Self {
        DBError::Bincode(e)
    }
}

impl From<anyhow::Error> for DBError {
    fn from(e: anyhow::Error) -> Self {
        DBError::Custom(e)
    }
}
