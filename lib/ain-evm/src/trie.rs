use ethereum_types::H256;
use hash_db::{AsHashDB, HashDB, HashDBRef, Hasher as _, Prefix};
use kvdb::{DBValue, KeyValueDB};
use kvdb_rocksdb::{Database, DatabaseConfig};
use sp_core::hexdisplay::AsBytesRef;
use sp_core::Blake2Hasher;
use sp_trie::{LayoutV1, TrieDBMutBuilder, TrieHash};
use sp_trie::{NodeCodec, TrieMut};
use std::path::PathBuf;
use std::sync::Arc;
use trie_db::{NodeCodec as _, Trie, TrieDB, TrieDBBuilder, TrieDBMut};

pub static ROCKSDB_PATH: &str = "state_trie.db";

type Hasher = Blake2Hasher;

pub struct TrieBackend {
    pub db: Arc<dyn KeyValueDB>,
}

impl AsHashDB<Hasher, DBValue> for TrieBackend {
    fn as_hash_db(&self) -> &dyn hash_db::HashDB<Hasher, DBValue> {
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
        self.db.get(0, &key).expect("Database error")
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
        let mut transaction = self.db.transaction();
        transaction.put_vec(0, &key, value);
        self.db.write(transaction).expect("Database error")
    }

    fn remove(&mut self, key: &H256, prefix: Prefix) {
        let key = sp_trie::prefixed_key::<Hasher>(key, prefix);
        let mut transaction = self.db.transaction();
        transaction.delete(0, &key);
        self.db.write(transaction).expect("Database error")
    }
}

type L = LayoutV1<Hasher>;
pub type TrieRoot = TrieHash<L>;
type Error = TrieError;
type Result<T> = std::result::Result<T, Error>;

impl TrieBackend {
    const COLUMNS: u32 = 1;

    pub fn new(path: PathBuf) -> Self {
        let config = DatabaseConfig::default();
        let db = Database::open(&config, path).expect("Failed to open database");

        Self { db: Arc::new(db) }
    }
}

impl Default for TrieBackend {
    fn default() -> Self {
        Self::new(PathBuf::from(ROCKSDB_PATH))
    }
}

pub struct StateTrie<'a> {
    trie: TrieDB<'a, 'a, L>,
}

impl<'a> StateTrie<'a> {
    pub fn new(backend: &'a TrieBackend, root: &'a TrieRoot) -> Self {
        let trie = TrieDBBuilder::new(backend, root).build();
        Self { trie }
    }

    pub fn get(&self, key: &[u8]) -> Result<Option<Vec<u8>>> {
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

pub struct StateTrieMut<'a> {
    trie: TrieDBMut<'a, L>,
}

impl<'a> StateTrieMut<'a> {
    pub fn new(backend: &'a mut TrieBackend, root: &'a mut TrieRoot) -> Self {
        let trie = TrieDBMutBuilder::new(backend, root).build();
        Self { trie }
    }

    pub fn from_existing(backend: &'a mut TrieBackend, root: &'a mut TrieRoot) -> Self {
        let trie = TrieDBMutBuilder::from_existing(backend, root).build();
        Self { trie }
    }

    pub fn get(&self, key: &[u8]) -> Result<Option<Vec<u8>>> {
        self.trie.get(key).map_err(TrieError::from)
    }

    pub fn contains(&self, key: &[u8]) -> Result<bool> {
        self.trie.contains(key).map_err(TrieError::from)
    }

    pub fn is_empty(&self) -> bool {
        self.trie.is_empty()
    }

    fn insert(
        &mut self,
        key: H256,
        value: DBValue,
    ) -> Result<Option<trie_db::Value<LayoutV1<Hasher>>>> {
        self.trie
            .insert(key.as_bytes(), value.as_bytes_ref())
            .map_err(TrieError::from)
    }

    fn remove(&mut self, key: &H256) -> Result<Option<trie_db::Value<LayoutV1<Hasher>>>> {
        self.trie.remove(&key.as_bytes()).map_err(TrieError::from)
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

#[cfg(test)]
mod tests {
    use std::sync::Mutex;

    use super::*;
    use once_cell::sync::OnceCell;
    use tempdir::TempDir;

    static BACKEND: OnceCell<Mutex<TrieBackend>> = OnceCell::new();

    fn setup_backend() -> TrieRoot {
        let mut backend = BACKEND
            .get_or_init(|| {
                let tmp_dir = TempDir::new("trie_backend_test").unwrap();

                let backend = TrieBackend::new(tmp_dir.path().to_path_buf());
                Mutex::new(backend)
            })
            .lock()
            .unwrap();
        let mut state_root = TrieRoot::default();
        let mut trie_mut = StateTrieMut::new(&mut backend, &mut state_root);
        trie_mut.root().clone()
    }

    #[test]
    fn test_state_trie_empty() -> Result<()> {
        let state_root = setup_backend();
        let backend = BACKEND.get().unwrap().lock().unwrap();
        let trie = StateTrie::new(&backend, &state_root);

        assert!(trie.is_empty());
        Ok(())
    }

    #[test]
    fn test_state_trie_get_contains() -> Result<()> {
        let mut state_root = setup_backend();
        let mut backend = BACKEND.get().unwrap().lock().unwrap();
        let trie_mut = StateTrieMut::from_existing(&mut backend, &mut state_root);

        let key = H256::random();

        assert_eq!(trie_mut.get(&key.as_bytes())?, None);
        assert!(!trie_mut.contains(&key.as_bytes())?);
        Ok(())
    }

    #[test]
    fn test_state_trie_mut_insert_remove() -> Result<()> {
        let mut state_root = setup_backend();
        let mut backend = BACKEND.get().unwrap().lock().unwrap();
        let mut trie_mut = StateTrieMut::from_existing(&mut backend, &mut state_root);

        let key = H256::random();
        let value = b"test".to_vec();

        trie_mut.insert(key, value.clone())?;
        assert_eq!(trie_mut.get(&key.as_bytes())?.unwrap(), value);

        trie_mut.remove(&key)?;
        assert_eq!(trie_mut.get(&key.as_bytes())?, None);
        Ok(())
    }

    #[test]
    fn test_state_trie_root_consistency() -> Result<()> {
        let mut state_root = setup_backend();
        let mut backend = BACKEND.get().unwrap().lock().unwrap();
        let mut trie_mut = StateTrieMut::from_existing(&mut backend, &mut state_root);

        let key1 = H256::random();
        let value1 = b"test1".to_vec();
        let key2 = H256::random();
        let value2 = b"test2".to_vec();

        let initial_root = trie_mut.root();
        trie_mut.insert(key1, value1.clone())?;
        trie_mut.insert(key2, value2.clone())?;

        let updated_root = trie_mut.root();
        assert_ne!(initial_root, updated_root);

        trie_mut.remove(&key1)?;
        trie_mut.remove(&key2)?;

        let final_root = trie_mut.root();
        assert_eq!(initial_root, final_root);
        Ok(())
    }

    #[test]
    fn test_state_trie_mut_remove_non_existent() -> Result<()> {
        let mut state_root = setup_backend();
        let mut backend = BACKEND.get().unwrap().lock().unwrap();
        let mut trie_mut = StateTrieMut::from_existing(&mut backend, &mut state_root);

        let key = H256::random();
        assert_eq!(trie_mut.remove(&key)?, None);
        Ok(())
    }

    #[test]
    fn test_state_trie_mut_insert_duplicate() -> Result<()> {
        let mut state_root = setup_backend();
        let mut backend = BACKEND.get().unwrap().lock().unwrap();
        let mut trie_mut = StateTrieMut::from_existing(&mut backend, &mut state_root);

        let key = H256::random();
        let value = b"test".to_vec();

        trie_mut.insert(key, value.clone())?;
        assert_eq!(trie_mut.get(&key.as_bytes())?.unwrap(), value);

        trie_mut.insert(key, value.clone())?;
        assert_eq!(trie_mut.get(&key.as_bytes())?.unwrap(), value);

        Ok(())
    }

    #[test]
    fn test_state_trie_mut_update() -> Result<()> {
        let mut state_root = setup_backend();
        let mut backend = BACKEND.get().unwrap().lock().unwrap();
        let mut trie_mut = StateTrieMut::from_existing(&mut backend, &mut state_root);

        let key = H256::random();
        let value1 = b"test1".to_vec();
        let value2 = b"test2".to_vec();

        trie_mut.insert(key, value1.clone())?;
        assert_eq!(trie_mut.get(&key.as_bytes())?.unwrap(), value1);

        trie_mut.insert(key, value2.clone())?;
        assert_eq!(trie_mut.get(&key.as_bytes())?.unwrap(), value2);

        Ok(())
    }
}
