use ethereum::{Account, Log};
use evm::backend::{Apply, ApplyBackend, Backend, Basic};
use hash_db::Hasher as _;
use primitive_types::{H160, H256, U256};
use rlp::{Decodable, Encodable, Rlp};
use sp_core::hexdisplay::AsBytesRef;
use sp_core::Blake2Hasher;
use vsdb_trie_db::{MptOnce, MptStore};

use crate::storage::Storage;

type MerkleRoot = H256;
type Hasher = Blake2Hasher;

fn is_empty_account(account: &Account) -> bool {
    account.balance.is_zero() && account.nonce.is_zero() && account.code_hash.is_zero()
}

pub struct Vicinity {
    gas_price: U256,
    origin: H160,
    logs: Vec<Log>,
}

pub struct EVMBackend<'a> {
    state: MptOnce,
    trie_db: &'a MptStore,
    storage: &'a Storage,
    vicinity: Vicinity,
}

type Result<T> = std::result::Result<T, EVMBackendError>;

impl<'a> EVMBackend<'a> {
    pub fn new(
        trie_db: &'a MptStore,
        storage: &'a Storage,
        vicinity: Vicinity,
        cache_size: Option<usize>,
    ) -> Result<Self> {
        let state = trie_db
            .trie_create(&[0], cache_size, false)
            .map_err(|_| EVMBackendError::TrieCreationFailed)?;

        Ok(EVMBackend {
            state,
            trie_db,
            storage,
            vicinity,
        })
    }

    pub fn from_root(
        state_root: MerkleRoot,
        trie_db: &'a MptStore,
        storage: &'a Storage,
        vicinity: Vicinity,
    ) -> Result<Self> {
        let state = trie_db
            .trie_restore(&[0], None, state_root.into())
            .map_err(|_| EVMBackendError::TrieCreationFailed)?;

        Ok(EVMBackend {
            state,
            trie_db,
            storage,
            vicinity,
        })
    }

    pub fn apply<I: IntoIterator<Item = (H256, H256)>>(
        &mut self,
        address: H160,
        basic: Basic,
        code: Option<Vec<u8>>,
        storage: I,
        reset_storage: bool,
    ) -> Result<bool> {
        let account = self.get_account(address).unwrap_or(Account {
            nonce: U256::zero(),
            balance: U256::zero(),
            storage_root: H256::zero(),
            code_hash: H256::zero(),
        });

        let mut storage_trie = if reset_storage || is_empty_account(&account) {
            self.trie_db
                .trie_create(address.as_bytes(), None, true)
                .map_err(|_| EVMBackendError::TrieCreationFailed)?
        } else {
            self.trie_db
                .trie_restore(address.as_bytes(), None, account.storage_root.into())
                .map_err(|_| EVMBackendError::TrieRestoreFailed)?
        };

        storage.into_iter().for_each(|(k, v)| {
            let _ = storage_trie.insert(k.as_bytes(), v.as_bytes());
        });

        let code_hash = code
            .map(|code| {
                let code_hash = Hasher::hash(&code);
                self.storage.put_code(code_hash, code);
                code_hash
            })
            .unwrap_or_default();

        let new_account = Account {
            nonce: basic.nonce,
            balance: basic.balance,
            code_hash: code_hash,
            storage_root: storage_trie.commit().into(),
        };

        self.state
            .insert(address.as_bytes(), new_account.rlp_bytes().as_ref())
            .map_err(|e| EVMBackendError::TrieError(format!("{}", e)))?;

        Ok(is_empty_account(&new_account))
    }

    pub fn commit(&mut self) -> MerkleRoot {
        self.state.commit().into()
    }
}

impl EVMBackend<'_> {
    pub fn get_account(&self, address: H160) -> Option<Account> {
        self.state
            .get(address.as_bytes())
            .unwrap_or(None)
            .and_then(|addr| Account::decode(&Rlp::new(addr.as_bytes_ref())).ok())
    }
}

impl<'a> Backend for EVMBackend<'a> {
    fn gas_price(&self) -> U256 {
        self.vicinity.gas_price
    }

    fn origin(&self) -> H160 {
        self.vicinity.origin
    }

    fn block_hash(&self, number: U256) -> H256 {
        unimplemented!("Implement block_hash function")
    }

    fn block_number(&self) -> U256 {
        unimplemented!("Implement block_number function")
    }

    fn block_coinbase(&self) -> H160 {
        unimplemented!("Implement block_coinbase function")
    }

    fn block_timestamp(&self) -> U256 {
        unimplemented!("Implement block_timestamp function")
    }

    fn block_difficulty(&self) -> U256 {
        U256::zero()
    }

    fn block_gas_limit(&self) -> U256 {
        unimplemented!("Implement block_gas_limit function")
    }

    fn block_base_fee_per_gas(&self) -> U256 {
        unimplemented!("Implement block_base_fee_per_gas function")
    }

    fn chain_id(&self) -> U256 {
        U256::from(ain_cpp_imports::get_chain_id().expect("Error getting chain_id"))
    }

    fn exists(&self, address: H160) -> bool {
        self.state.contains(address.as_bytes()).unwrap_or_default()
    }

    fn basic(&self, address: H160) -> Basic {
        self.get_account(address)
            .and_then(|account| {
                Some(Basic {
                    balance: account.balance,
                    nonce: account.nonce,
                })
            })
            .unwrap_or_default()
    }

    fn code(&self, address: H160) -> Vec<u8> {
        self.get_account(address)
            .and_then(|account| self.storage.get_code_by_hash(account.code_hash))
            .unwrap_or_default()
    }

    fn storage(&self, address: H160, index: H256) -> H256 {
        self.get_account(address)
            .and_then(|account| {
                self.trie_db
                    .trie_restore(address.as_bytes(), None, account.storage_root.into())
                    .ok()
            })
            .and_then(|trie| trie.get(index.as_bytes()).ok().flatten())
            .map(|res| H256::from_slice(res.as_ref()))
            .unwrap_or_default()
    }

    fn original_storage(&self, address: H160, index: H256) -> Option<H256> {
        Some(self.storage(address, index))
    }
}

impl<'a> ApplyBackend for EVMBackend<'a> {
    fn apply<A, I, L>(&mut self, values: A, logs: L, delete_empty: bool)
    where
        A: IntoIterator<Item = Apply<I>>,
        I: IntoIterator<Item = (H256, H256)>,
        L: IntoIterator<Item = Log>,
    {
        for apply in values.into_iter() {
            match apply {
                Apply::Modify {
                    address,
                    basic,
                    code,
                    storage,
                    reset_storage,
                } => {
                    let is_empty = self
                        .apply(address, basic, code, storage, reset_storage)
                        .expect("Error applying state");

                    if is_empty && delete_empty {
                        self.trie_db.trie_remove(address.as_bytes());
                        self.state
                            .remove(address.as_bytes())
                            .expect("Error removing address in state");
                    }
                }
                Apply::Delete { address } => {
                    self.trie_db.trie_remove(address.as_bytes());
                    self.state
                        .remove(address.as_bytes())
                        .expect("Error removing address in state");
                }
            }
        }

        self.vicinity.logs = logs.into_iter().collect::<Vec<_>>();
    }
}

use std::{error::Error, fmt};

#[derive(Debug)]
pub enum EVMBackendError {
    TrieCreationFailed,
    TrieRestoreFailed,
    TrieError(String),
}

impl fmt::Display for EVMBackendError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            EVMBackendError::TrieCreationFailed => write!(f, "Failed to create trie"),
            EVMBackendError::TrieRestoreFailed => write!(f, "Failed to restore trie"),
            EVMBackendError::TrieError(e) => write!(f, "Trie error {}", e),
        }
    }
}

impl Error for EVMBackendError {}
