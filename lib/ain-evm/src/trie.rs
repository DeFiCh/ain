use std::{fs, io::BufReader, path::PathBuf, sync::Arc};

use ethereum_types::H256;
use evm::backend::{Backend, Basic};
use log::debug;
use serde::{Deserialize, Serialize};
use vsdb_trie_db::MptStore;

use crate::{
    backend::{EVMBackend, Vicinity},
    genesis::GenesisData,
    storage::{traits::PersistentState, Storage},
    Result,
};

pub static TRIE_DB_STORE: &str = "trie_db_store.bin";
pub static GENESIS_STATE_ROOT: H256 = H256([
    188, 54, 120, 158, 122, 30, 40, 20, 54, 70, 66, 41, 130, 143, 129, 125, 102, 18, 247, 180, 119,
    214, 101, 145, 255, 150, 169, 224, 100, 188, 201, 138,
]);

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
    /// This function should only be used in a regtest environment.
    /// Can conflict with existing chain state if used on another network
    pub fn genesis_state_root_from_json(
        trie_store: &Arc<TrieDBStore>,
        storage: &Arc<Storage>,
        json_file: PathBuf,
    ) -> Result<(H256, GenesisData)> {
        let state_root: H256 = GENESIS_STATE_ROOT;

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(trie_store),
            Arc::clone(storage),
            Vicinity::default(),
            None,
        )
        .expect("Could not restore backend");

        let file = fs::File::open(json_file)?;
        let reader = BufReader::new(file);
        let genesis: GenesisData = serde_json::from_reader(reader)?;

        if let Some(alloc) = genesis.clone().alloc {
            for (address, data) in alloc {
                debug!("Setting data {:#?} for address {:x?}", data, address);
                let basic = backend.basic(address);

                let new_basic = Basic {
                    balance: data.balance,
                    ..basic
                };
                backend
                    .apply(
                        address,
                        Some(new_basic),
                        data.code,
                        data.storage.unwrap_or_default(),
                        false,
                    )
                    .expect("Could not set account data");
            }
        }

        let state_root = backend.commit(false)?;
        debug!("Loaded genesis state_root : {:#x}", state_root);
        Ok((state_root, genesis))
    }

    pub fn flush(&self) -> Result<()> {
        self.save_to_disk(TRIE_DB_STORE)
    }
}
