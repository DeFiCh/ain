use ethereum::{Account, Log};
use evm::backend::{Apply, ApplyBackend, Backend, Basic};
use hash_db::Hasher as _;
use log::debug;
use primitive_types::{H160, H256, U256};
use rlp::{Decodable, Encodable, Rlp};
use sp_core::hexdisplay::AsBytesRef;
use sp_core::Blake2Hasher;
use vsdb_trie_db::MptOnce;

use crate::{evm::TrieDBStore, storage::Storage, traits::BridgeBackend};

type Hasher = Blake2Hasher;

fn is_empty_account(account: &Account) -> bool {
    account.balance.is_zero() && account.nonce.is_zero() && account.code_hash.is_zero()
}

// TBD
pub struct Vicinity;

pub struct EVMBackend {
    state: MptOnce,
    trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
    _vicinity: Vicinity,
}

type Result<T> = std::result::Result<T, EVMBackendError>;

impl EVMBackend {
    pub fn from_root(
        state_root: H256,
        trie_store: Arc<TrieDBStore>,
        storage: Arc<Storage>,
        _vicinity: Vicinity,
    ) -> Result<Self> {
        let state = trie_store
            .trie_db
            .trie_restore(&[0], None, state_root.into())
            .map_err(|e| EVMBackendError::TrieRestoreFailed(e.to_string()))?;

        Ok(EVMBackend {
            state,
            trie_store,
            storage,
            _vicinity,
        })
    }

    pub fn apply<I: IntoIterator<Item = (H256, H256)>>(
        &mut self,
        address: H160,
        basic: Basic,
        code: Option<Vec<u8>>,
        storage: I,
        reset_storage: bool,
    ) -> Result<Account> {
        let account = self.get_account(address).unwrap_or(Account {
            nonce: U256::zero(),
            balance: U256::zero(),
            storage_root: H256::zero(),
            code_hash: H256::zero(),
        });

        let mut storage_trie = if reset_storage || is_empty_account(&account) {
            self.trie_store
                .trie_db
                .trie_create(address.as_bytes(), None, true)
                .map_err(|e| EVMBackendError::TrieCreationFailed(e.to_string()))?
        } else {
            self.trie_store
                .trie_db
                .trie_restore(address.as_bytes(), None, account.storage_root.into())
                .map_err(|e| EVMBackendError::TrieRestoreFailed(e.to_string()))?
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
            .unwrap_or(account.code_hash);

        let new_account = Account {
            nonce: basic.nonce,
            balance: basic.balance,
            code_hash,
            storage_root: storage_trie.commit().into(),
        };

        self.state
            .insert(address.as_bytes(), new_account.rlp_bytes().as_ref())
            .map_err(|e| EVMBackendError::TrieError(format!("{}", e)))?;

        Ok(new_account)
    }

    pub fn commit(&mut self) -> H256 {
        self.state.commit().into()
    }
}

impl EVMBackend {
    pub fn get_account(&self, address: H160) -> Option<Account> {
        self.state
            .get(address.as_bytes())
            .unwrap_or(None)
            .and_then(|addr| Account::decode(&Rlp::new(addr.as_bytes_ref())).ok())
    }
}

impl Backend for EVMBackend {
    fn gas_price(&self) -> U256 {
        debug!(target: "backend", "[EVMBackend] Getting gas");
        unimplemented!()
    }

    fn origin(&self) -> H160 {
        debug!(target: "backend", "[EVMBackend] Getting origin");
        unimplemented!()
    }

    fn block_hash(&self, _number: U256) -> H256 {
        unimplemented!("Implement block_hash function")
    }

    fn block_number(&self) -> U256 {
        unimplemented!()
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
        debug!(target: "backend", "[EVMBackend] Getting block_base_fee_per_gas");
        U256::default()
        // unimplemented!("Implement block_base_fee_per_gas function")
    }

    fn chain_id(&self) -> U256 {
        U256::from(ain_cpp_imports::get_chain_id().expect("Error getting chain_id"))
    }

    fn exists(&self, address: H160) -> bool {
        self.state.contains(address.as_bytes()).unwrap_or(false)
    }

    fn basic(&self, address: H160) -> Basic {
        debug!(target: "backend", "[EVMBackend] basic for address {:x?}", address);
        self.get_account(address)
            .map(|account| Basic {
                balance: account.balance,
                nonce: account.nonce,
            })
            .unwrap_or_default()
    }

    fn code(&self, address: H160) -> Vec<u8> {
        debug!(target: "backend", "[EVMBackend] code for address {:x?}", address);
        self.get_account(address)
            .and_then(|account| self.storage.get_code_by_hash(account.code_hash))
            .unwrap_or_default()
    }

    fn storage(&self, address: H160, index: H256) -> H256 {
        debug!(target: "backend", "[EVMBackend] Getting storage for address {:x?} at index {:x?}", address, index);
        self.get_account(address)
            .and_then(|account| {
                self.trie_store
                    .trie_db
                    .trie_restore(address.as_bytes(), None, account.storage_root.into())
                    .ok()
            })
            .and_then(|trie| trie.get(index.as_bytes()).ok().flatten())
            .map(|res| H256::from_slice(res.as_ref()))
            .unwrap_or_default()
    }

    fn original_storage(&self, address: H160, index: H256) -> Option<H256> {
        debug!(target: "backend", "[EVMBackend] Getting original storage for address {:x?} at index {:x?}", address, index);
        Some(self.storage(address, index))
    }
}

impl ApplyBackend for EVMBackend {
    fn apply<A, I, L>(&mut self, values: A, _logs: L, delete_empty: bool)
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
                    let new_account = self
                        .apply(address, basic, code, storage, reset_storage)
                        .expect("Error applying state");

                    if is_empty_account(&new_account) && delete_empty {
                        debug!("Deleting empty address {}", address);
                        self.trie_store.trie_db.trie_remove(address.as_bytes());
                        self.state
                            .remove(address.as_bytes())
                            .expect("Error removing address in state");
                    }
                }
                Apply::Delete { address } => {
                    debug!("Deleting address {}", address);
                    self.trie_store.trie_db.trie_remove(address.as_bytes());
                    self.state
                        .remove(address.as_bytes())
                        .expect("Error removing address in state");
                }
            }
        }
    }
}

impl BridgeBackend for EVMBackend {
    fn add_balance(&mut self, address: H160, amount: U256) -> Result<()> {
        let basic = self.basic(address);

        let new_basic = Basic {
            balance: basic.balance + amount,
            ..basic
        };

        self.apply(address, new_basic, None, Vec::new(), false)?;
        self.state.commit();
        Ok(())
    }

    fn sub_balance(&mut self, address: H160, amount: U256) -> Result<()> {
        let account = self
            .get_account(address)
            .ok_or(EVMBackendError::NoSuchAccount(address))?;

        if account.balance < amount {
            Err(EVMBackendError::InsufficientBalance(InsufficientBalance {
                address,
                account_balance: account.balance,
                amount,
            }))
        } else {
            let new_basic = Basic {
                balance: account.balance - amount,
                nonce: account.nonce,
            };

            self.apply(address, new_basic, None, Vec::new(), false)?;
            self.state.commit();
            Ok(())
        }
    }
}

use std::{error::Error, fmt, sync::Arc};

#[derive(Debug)]
pub struct InsufficientBalance {
    address: H160,
    account_balance: U256,
    amount: U256,
}

#[derive(Debug)]
pub enum EVMBackendError {
    TrieCreationFailed(String),
    TrieRestoreFailed(String),
    TrieError(String),
    NoSuchAccount(H160),
    InsufficientBalance(InsufficientBalance),
}

impl fmt::Display for EVMBackendError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            EVMBackendError::TrieCreationFailed(e) => {
                write!(f, "EVMBackendError: Failed to create trie {}", e)
            }
            EVMBackendError::TrieRestoreFailed(e) => {
                write!(f, "EVMBackendError: Failed to restore trie {}", e)
            }
            EVMBackendError::TrieError(e) => write!(f, "EVMBackendError: Trie error {}", e),
            EVMBackendError::NoSuchAccount(address) => {
                write!(
                    f,
                    "EVMBackendError: No such acccount for address {}",
                    address
                )
            }
            EVMBackendError::InsufficientBalance(InsufficientBalance {
                address,
                account_balance,
                amount,
            }) => {
                write!(f, "EVMBackendError: Insufficient balance for address {}, trying to deduct {} but address has only {}", address, amount, account_balance)
            }
        }
    }
}

impl Error for EVMBackendError {}
