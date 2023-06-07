use crate::backend::{EVMBackend, Vicinity};
use crate::genesis::GenesisData;
use crate::storage::traits::{PersistentState, PersistentStateError};
use crate::storage::Storage;

use evm::backend::{Backend, Basic};
use log::debug;
use primitive_types::H256;
use serde::{Deserialize, Serialize};
use std::fs;
use std::io::BufReader;
use std::path::PathBuf;
use std::sync::Arc;
use vsdb_trie_db::MptStore;

pub static TRIE_DB_STORE: &str = "trie_db_store.bin";
pub static GENESIS_STATE_ROOT: &str =
    "0xbc36789e7a1e281436464229828f817d6612f7b477d66591ff96a9e064bcc98a";

#[derive(Serialize, Deserialize)]
pub struct TrieDBStore {
    pub trie_db: MptStore,
}

impl PersistentState for TrieDBStore {}

impl Default for TrieDBStore {
    fn default() -> Self {
        Self::new()
    }
}

impl TrieDBStore {
    pub fn new() -> Self {
        debug!("Creating new trie store");
        let trie_store = MptStore::new();
        let mut trie = trie_store
            .trie_create(&[0], None, false)
            .expect("Error creating initial backend");
        let state_root: H256 = trie.commit().into();
        debug!("Initial state_root : {:#x}", state_root);
        Self {
            trie_db: trie_store,
        }
    }

    pub fn restore() -> Self {
        TrieDBStore::load_from_disk(TRIE_DB_STORE).expect("Error loading trie db store")
    }

    /// # Warning
    ///
    /// This function should only be used in a regtest environment. Can conflict with existing chain state if used
    /// on another network
    pub fn genesis_state_root_from_json(
        trie_store: &Arc<TrieDBStore>,
        storage: &Arc<Storage>,
        json_file: PathBuf,
    ) -> Result<H256, std::io::Error> {
        let state_root: H256 = GENESIS_STATE_ROOT.parse().unwrap();

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(trie_store),
            Arc::clone(storage),
            Vicinity::default(),
        )
        .expect("Could not restore backend");

        let file = fs::File::open(json_file)?;
        let reader = BufReader::new(file);
        let genesis: GenesisData = serde_json::from_reader(reader)?;

        for (address, data) in genesis.alloc {
            let basic = backend.basic(address);

            let new_basic = Basic {
                balance: data.balance,
                ..basic
            };
            backend
                .apply(
                    address,
                    new_basic,
                    data.code,
                    data.storage.unwrap_or_default(),
                    false,
                )
                .expect("Could not set account data");
            backend.commit();
        }

        let state_root: H256 = backend.commit().into();
        debug!("Loaded genesis state_root : {:#x}", state_root);
        Ok(state_root)
    }

    pub fn flush(&self) -> Result<(), PersistentStateError> {
        self.save_to_disk(TRIE_DB_STORE)
    }
}
