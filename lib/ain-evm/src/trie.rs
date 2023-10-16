use std::{path::PathBuf, sync::Arc};

use ethereum::Account;
use ethereum_types::{H160, H256, U256};
use hash_db::{AsHashDB, HashDB, HashDBRef, Hasher as _, Prefix};
use kvdb::{DBValue, KeyValueDB};
use log::debug;
use rlp::Encodable;
use rocksdb::{
    BlockBasedOptions, Cache, ColumnFamily, ColumnFamilyDescriptor, FlushOptions, Options,
    WriteBatch, WriteOptions, DB,
};
use sp_core::{hexdisplay::AsBytesRef, KeccakHasher};
use sp_trie::{LayoutV1, NodeCodec, TrieDBMutBuilder, TrieHash, TrieMut as _};
use trie_db::{NodeCodec as _, Trie as _, TrieDB, TrieDBBuilder, TrieDBMut};

pub static ROCKSDB_PATH: &str = "trie.db";
pub static GENESIS_STATE_ROOT: H256 = H256([
    188, 54, 120, 158, 122, 30, 40, 20, 54, 70, 66, 41, 130, 143, 129, 125, 102, 18, 247, 180, 119,
    214, 101, 145, 255, 150, 169, 224, 100, 188, 201, 138,
]);

type Hasher = KeccakHasher;

pub struct TrieBackend(DB);

impl AsHashDB<Hasher, DBValue> for TrieBackend {
    fn as_hash_db(&self) -> &dyn HashDB<Hasher, DBValue> {
        &*self
    }

    fn as_hash_db_mut<'a>(&'a mut self) -> &'a mut (dyn HashDB<Hasher, DBValue> + 'a) {
        &mut *self
    }
}

impl HashDBRef<Hasher, DBValue> for TrieBackend {
    fn get(&self, key: &H256, prefix: Prefix) -> Option<DBValue> {
        HashDB::get(self, key, prefix)
    }
    fn contains(&self, key: &H256, prefix: Prefix) -> bool {
        HashDB::contains(self, key, prefix)
    }
}

impl HashDB<Hasher, DBValue> for TrieBackend {
    fn get(&self, key: &H256, prefix: Prefix) -> Option<DBValue> {
        if key == &NodeCodec::<Hasher>::hashed_null_node() {
            return Some([0u8].to_vec());
        }

        let key = sp_trie::prefixed_key::<Hasher>(key, prefix);
        self.0.get(&key).expect("Database error")
    }

    fn contains(&self, key: &H256, prefix: Prefix) -> bool {
        HashDB::get(self, key, prefix).is_some()
    }

    fn insert(&mut self, prefix: Prefix, value: &[u8]) -> H256 {
        let key = Hasher::hash(value);
        HashDB::emplace(self, key, prefix, DBValue::from(value));

        key
    }

    fn emplace(&mut self, key: H256, prefix: Prefix, value: DBValue) {
        let key = sp_trie::prefixed_key::<Hasher>(&key, prefix);
        // let mut transaction = self.0.transaction();
        // transaction.put_vec(0, &key, value);
        // self.0.write(transaction).expect("Database error")

        let mut batch = WriteBatch::default();
        batch.put(&key, &value);

        let mut write_options = WriteOptions::default();
        write_options.set_sync(true);

        self.0
            .write_opt(batch, &write_options)
            .expect("Database error")
    }

    fn remove(&mut self, key: &H256, prefix: Prefix) {
        let key = sp_trie::prefixed_key::<Hasher>(key, prefix);
        // let mut transaction = self.0.transaction();
        // transaction.delete(0, &key);
        // self.0.write(transaction).expect("Database error")

        let mut batch = WriteBatch::default();
        batch.delete(&key);

        let mut write_options = WriteOptions::default();
        write_options.set_sync(true); // Ensures the write is flushed to disk

        self.0
            .write_opt(batch, &write_options)
            .expect("Database error")
    }
}

type L = LayoutV1<Hasher>;
pub type TrieRoot = TrieHash<L>;
type Error = TrieError;
type Result<T> = std::result::Result<T, Error>;

impl TrieBackend {
    pub fn new(path: PathBuf) -> Result<Self> {
        // let path = ROCKSDB_PATH;
        let datadir = ain_cpp_imports::get_datadir();
        let dir = PathBuf::from(datadir).join("evm");
        if !dir.exists() {
            std::fs::create_dir(&dir).expect("Failed to create database path");
        }

        let db = DB::open_default(dir.join(path)).expect("Error opening rocksdb trie");
        Ok(Self(db))
    }

    // pub fn flush(&self) -> Result<()> {
    pub fn flush(&self) {
        let mut flush_options = FlushOptions::default();
        flush_options.set_wait(true);

        self.0
            .flush_opt(&flush_options)
            .expect("Error flushing rocksdb");
        // Ok(())
    }
}

pub struct Trie<'a> {
    trie: TrieDB<'a, 'a, L>,
}

impl<'a> Trie<'a> {
    pub fn new(backend: &'a TrieBackend, root: &'a TrieRoot) -> Self {
        debug!("Reading trie with state root : {:?}", root);

        let trie = TrieDBBuilder::new(backend, root).build();
        Self { trie }
    }

    pub fn get(&self, key: &[u8]) -> Result<Option<DBValue>> {
        self.trie.get(key).map_err(TrieError::from)
    }

    pub fn contains(&self, key: &[u8]) -> Result<bool> {
        self.trie.contains(key).map_err(TrieError::from)
    }

    pub fn is_empty(&self) -> bool {
        self.trie.is_empty()
    }

    pub fn root(&self) -> H256 {
        *self.trie.root()
    }
}

pub struct TrieMut<'a> {
    trie: TrieDBMut<'a, L>,
}

impl<'a> TrieMut<'a> {
    pub fn new(backend: &'a mut TrieBackend, root: &'a mut TrieRoot) -> Self {
        // debug!("Creating trie mut with state root : {:?}", root);

        let trie = TrieDBMutBuilder::new(backend, root).build();
        Self { trie }
    }

    pub fn from_existing(backend: &'a mut TrieBackend, root: &'a mut TrieRoot) -> Self {
        debug!(
            "Restoring from existing trie mut with state root : {:?}",
            root
        );
        let trie = TrieDBMutBuilder::from_existing(backend, root).build();
        Self { trie }
    }

    pub fn get(&self, key: &[u8]) -> Result<Option<DBValue>> {
        self.trie.get(key).map_err(TrieError::from)
    }

    pub fn contains(&self, key: &[u8]) -> Result<bool> {
        self.trie.contains(key).map_err(TrieError::from)
    }

    pub fn is_empty(&self) -> bool {
        self.trie.is_empty()
    }

    pub fn insert(
        &mut self,
        key: &[u8],
        value: &[u8],
    ) -> Result<Option<trie_db::Value<LayoutV1<Hasher>>>> {
        self.trie.insert(key, value).map_err(TrieError::from)
    }

    pub fn remove(&mut self, key: &[u8]) -> Result<Option<trie_db::Value<LayoutV1<Hasher>>>> {
        self.trie.remove(&key).map_err(TrieError::from)
    }

    pub fn root(&mut self) -> H256 {
        *self.trie.root()
    }
}

#[derive(Debug)]
pub enum TrieError {
    TrieDBError(trie_db::TrieError<H256, sp_trie::Error<H256>>),
}

impl From<Box<trie_db::TrieError<H256, sp_trie::Error<H256>>>> for TrieError {
    fn from(err: Box<trie_db::TrieError<H256, sp_trie::Error<H256>>>) -> TrieError {
        TrieError::TrieDBError(*err)
    }
}

use std::fmt;
impl fmt::Display for TrieError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            TrieError::TrieDBError(e) => {
                write!(f, "TrieError: Failed to create trie {e:?}")
            }
        }
    }
}

impl std::error::Error for TrieError {}
