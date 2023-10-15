use std::{error::Error, sync::Arc};

use anyhow::format_err;
use ethereum::{Account, Log};
use ethereum_types::{H160, H256, U256};
use evm::backend::{Apply, ApplyBackend, Backend, Basic};
use hash_db::Hasher as _;
use log::{debug, trace};
use parking_lot::RwLock;
use rlp::{Decodable, Encodable, Rlp};
use sp_core::{hexdisplay::AsBytesRef, Blake2Hasher};

use crate::{
    fee::calculate_gas_fee,
    storage::{traits::BlockStorage, Storage},
    transaction::SignedTx,
    trie::{Trie, TrieBackend, TrieError, TrieMut, TrieRoot},
    Result,
};

type Hasher = Blake2Hasher;

fn is_empty_account(account: &Account) -> bool {
    account.balance.is_zero() && account.nonce.is_zero() && account.code_hash.is_zero()
}

#[derive(Default, Debug, Clone)]
pub struct Vicinity {
    pub gas_price: U256,
    pub origin: H160,
    pub beneficiary: H160,
    pub block_number: U256,
    pub timestamp: U256,
    pub total_gas_used: U256,
    pub block_difficulty: U256,
    pub block_gas_limit: U256,
    pub block_base_fee_per_gas: U256,
    pub block_randomness: Option<H256>,
}

pub struct EVMBackendMut<'a> {
    pub storage_backend: Arc<RwLock<TrieBackend>>,
    pub state: *mut TrieMut<'a>,
    storage: Arc<Storage>,
    pub vicinity: Vicinity,
}

impl<'a> EVMBackendMut<'a> {
    pub fn from_root(
        storage_backend: Arc<RwLock<TrieBackend>>,
        state: *mut TrieMut<'a>,
        storage: Arc<Storage>,
        vicinity: Vicinity,
    ) -> Self {
        Self {
            storage_backend,
            state,
            storage,
            vicinity,
        }
    }

    pub fn apply<I: IntoIterator<Item = (H256, H256)>>(
        &mut self,
        address: H160,
        basic: Option<Basic>,
        code: Option<Vec<u8>>,
        storage: I,
        reset_storage: bool,
    ) -> Result<Account> {
        let mut account = self.get_account(&address).unwrap_or(Account {
            nonce: U256::zero(),
            balance: U256::zero(),
            storage_root: H256::zero(),
            code_hash: H256::zero(),
        });
        // println!("address : {:?}", address);
        // println!("account : {:?}", account);
        // println!("reset_storage : {:?}", reset_storage);

        // let mut base_root = H256::zero();

        let storage_root = {
            let mut storage_backend = self.storage_backend.write();
            let mut storage_trie = if reset_storage || is_empty_account(&account) {
                TrieMut::new(&mut storage_backend, &mut account.storage_root)
            } else {
                TrieMut::from_existing(&mut storage_backend, &mut account.storage_root)
            };

            storage
                .into_iter()
                .try_for_each(|(k, v)| {
                    debug!("Apply::Modify storage, key: {:x} value: {:x}", k, v);
                    let _ = storage_trie.insert(k.as_bytes(), &v.as_bytes()).unwrap();
                    Ok::<(), TrieError>(())
                })
                .map_err(|e| format_err!("{e}"))?;

            storage_trie.root()
        };

        let code_hash = match code {
            None => account.code_hash,
            Some(code) => {
                let code_hash = Hasher::hash(&code);
                self.storage.put_code(code_hash, code)?;
                code_hash
            }
        };

        let new_account = match basic {
            Some(basic) => Account {
                nonce: basic.nonce,
                balance: basic.balance,
                code_hash,
                storage_root,
            },
            None => Account {
                nonce: account.nonce,
                balance: account.balance,
                code_hash,
                storage_root,
            },
        };

        // println!("inserting new_account : {:?}", new_account);
        let mut state = unsafe { &mut *self.state };
        state
            .insert(&address.as_bytes(), &new_account.rlp_bytes())
            .unwrap();
        // .map_err(|e| BackendError::TrieError(format!("{e}")))?;

        // println!("inserted");
        Ok(new_account)
    }

    pub fn update_vicinity_from_tx(&mut self, tx: &SignedTx) {
        self.vicinity = Vicinity {
            origin: tx.sender,
            ..self.vicinity
        };
    }

    pub fn update_vicinity_with_gas_used(&mut self, gas_used: U256) {
        self.vicinity = Vicinity {
            total_gas_used: gas_used,
            ..self.vicinity
        };
    }

    pub fn root(&mut self) -> H256 {
        let mut state = unsafe { &mut *self.state };
        state.root()
    }
}

impl ApplyBackend for EVMBackendMut<'_> {
    fn apply<A, I, L>(&mut self, values: A, _logs: L, delete_empty: bool)
    where
        A: IntoIterator<Item = Apply<I>>,
        I: IntoIterator<Item = (H256, H256)>,
        L: IntoIterator<Item = Log>,
    {
        for apply in values {
            match apply {
                Apply::Modify {
                    address,
                    basic,
                    code,
                    storage,
                    reset_storage,
                } => {
                    debug!(
                        "Apply::Modify address {:x}, basic {:?}, code {:?}",
                        address, basic, code,
                    );

                    let new_account = self
                        .apply(address, Some(basic), code, storage, reset_storage)
                        .expect("Error applying state");

                    // if is_empty_account(&new_account) && delete_empty {
                    //     debug!("Deleting empty address {:x?}", address);
                    //     self.trie_store.trie_db.trie_remove(address.as_bytes());
                    //     self.state
                    //         .remove(address.as_bytes())
                    //         .expect("Error removing address in state");
                    // }
                }
                Apply::Delete { address } => {
                    debug!("Deleting address {:x?}", address);
                    // self.trie_store.trie_db.trie_remove(address.as_bytes());
                    // self.state
                    //     .remove(address.as_bytes())
                    //     .expect("Error removing address in state");
                }
            }
        }
    }
}

impl<'a> EVMBackendMut<'a> {
    pub fn with_read_storage_and_root<T, F: FnOnce(&Trie) -> T>(&self, root: &TrieRoot, f: F) -> T {
        let back = self.storage_backend.read();
        let trie = Trie::new(&*back, &root);
        f(&trie)
    }

    pub fn get_account(&self, address: &H160) -> Option<Account> {
        let state = unsafe { &*self.state };
        state
            .get(&address.as_bytes())
            .unwrap_or(None)
            .and_then(|addr| Account::decode(&Rlp::new(addr.as_bytes_ref())).ok())
    }

    pub fn get_nonce(&self, address: &H160) -> U256 {
        self.get_account(address)
            .map(|acc| acc.nonce)
            .unwrap_or_default()
    }

    pub fn get_balance(&self, address: &H160) -> U256 {
        self.get_account(address)
            .map(|acc| acc.balance)
            .unwrap_or_default()
    }

    pub fn get_contract_storage(&self, contract: H160, storage_index: &[u8]) -> Result<U256> {
        let Some(account) = self.get_account(&contract) else {
            println!("Not finding contract");
            return Ok(U256::zero());
        };

        self.with_read_storage_and_root(&account.storage_root, |trie| {
            println!("Trying to fetch account storage");
            let storage = U256::from(trie.get(storage_index)?.unwrap_or_default().as_slice());
            println!("storage : {:?}", storage);
            Ok(storage)
        })
    }

    pub fn deploy_contract(
        &mut self,
        address: &H160,
        code: Vec<u8>,
        storage: Vec<(H256, H256)>,
    ) -> Result<()> {
        // println!("deploying address : {:?}", address);
        self.apply(*address, None, Some(code), storage, true)?;

        Ok(())
    }

    pub fn update_storage(&mut self, address: &H160, storage: Vec<(H256, H256)>) -> Result<()> {
        self.apply(*address, None, None, storage, false)?;

        Ok(())
    }
}

impl Backend for EVMBackendMut<'_> {
    fn gas_price(&self) -> U256 {
        trace!(target: "backend", "[EVMBackendMut] Getting gas price");
        self.vicinity.gas_price
    }

    fn origin(&self) -> H160 {
        trace!(target: "backend", "[EVMBackendMut] Getting origin");
        self.vicinity.origin
    }

    fn block_hash(&self, number: U256) -> H256 {
        trace!(target: "backend", "[EVMBackendMut] Getting block hash for block {:x?}", number);
        self.storage
            .get_block_by_number(&number)
            .expect("Could not get block by number")
            .map_or(H256::zero(), |block| block.header.hash())
    }

    fn block_number(&self) -> U256 {
        trace!(target: "backend", "[EVMBackendMut] Getting current block number");
        self.vicinity.block_number
    }

    fn block_coinbase(&self) -> H160 {
        self.vicinity.beneficiary
    }

    fn block_timestamp(&self) -> U256 {
        self.vicinity.timestamp
    }

    fn block_difficulty(&self) -> U256 {
        self.vicinity.block_difficulty
    }

    fn block_randomness(&self) -> Option<H256> {
        self.vicinity.block_randomness
    }

    fn block_gas_limit(&self) -> U256 {
        self.vicinity.block_gas_limit
    }

    fn block_base_fee_per_gas(&self) -> U256 {
        trace!(target: "backend", "[EVMBackendMut] Getting block_base_fee_per_gas");
        self.vicinity.block_base_fee_per_gas
    }

    fn chain_id(&self) -> U256 {
        U256::from(ain_cpp_imports::get_chain_id().expect("Error getting chain_id"))
    }

    fn exists(&self, address: H160) -> bool {
        let state = unsafe { &*self.state };
        state.contains(&address.as_bytes()).unwrap_or(false)
    }

    fn basic(&self, address: H160) -> Basic {
        trace!(target: "backend", "[EVMBackendMut] basic for address {:x?}", address);
        self.get_account(&address)
            .map(|account| Basic {
                balance: account.balance,
                nonce: account.nonce,
            })
            .unwrap_or_default()
    }

    fn code(&self, address: H160) -> Vec<u8> {
        trace!(target: "backend", "[EVMBackendMut] code for address {:x?}", address);
        self.get_account(&address)
            .and_then(|account| self.storage.get_code_by_hash(account.code_hash).unwrap())
            .unwrap_or_default()
    }

    fn storage(&self, address: H160, index: H256) -> H256 {
        trace!(target: "backend", "[EVMBackendMut] Getting storage for address {:x?} at index {:x?}", address, index);
        self.get_account(&address)
            .and_then(|account| {
                self.with_read_storage_and_root(&account.storage_root, |trie| {
                    println!("Trying to fetch account storage for address {address:?}");
                    let storage =
                        trie.get(&index.as_bytes())
                            .ok()
                            .flatten()
                            .map_or_else(H256::zero, |vec| {
                                println!("vec : {:?}", vec);
                                println!("vec.len() : {:?}", vec.len());
                                H256::from_slice(vec.as_slice())
                            });
                    println!("storage : {:?}", storage);
                    Some(storage)
                })
            })
            .unwrap_or_default()
    }

    fn original_storage(&self, address: H160, index: H256) -> Option<H256> {
        trace!(target: "backend", "[EVMBackendMut] Getting original storage for address {:x?} at index {:x?}", address, index);
        Some(self.storage(address, index))
    }
}

impl EVMBackendMut<'_> {
    pub fn add_balance(&mut self, address: H160, amount: U256) -> Result<()> {
        let basic = self.basic(address);

        let new_basic = Basic {
            balance: basic
                .balance
                .checked_add(amount)
                .ok_or_else(|| format_err!("Balance overflow"))?,
            ..basic
        };

        self.apply(address, Some(new_basic), None, Vec::new(), false)?;
        Ok(())
    }

    pub fn sub_balance(&mut self, address: H160, amount: U256) -> Result<()> {
        let account = self
            .get_account(&address)
            .ok_or(BackendError::NoSuchAccount(address))?;

        if account.balance < amount {
            return Err(BackendError::InsufficientBalance(InsufficientBalance {
                address,
                account_balance: account.balance,
                amount,
            })
            .into());
        }

        let new_basic = Basic {
            balance: account.balance - amount, // sub is safe due to check above
            nonce: account.nonce,
        };

        self.apply(address, Some(new_basic), None, Vec::new(), false)?;
        Ok(())
    }

    pub fn deduct_prepay_gas_fee(&mut self, sender: H160, prepay_fee: U256) -> Result<()> {
        debug!(target: "backend", "[deduct_prepay_gas_fee] Deducting {:#x} from {:#x}", prepay_fee, sender);

        let basic = self.basic(sender);
        let balance = basic.balance.checked_sub(prepay_fee).ok_or_else(|| {
            BackendError::DeductPrepayGasFailed(String::from(
                "failed checked sub prepay gas with account balance",
            ))
        })?;
        let new_basic = Basic { balance, ..basic };

        self.apply(sender, Some(new_basic), None, Vec::new(), false)
            .map_err(|e| BackendError::DeductPrepayGasFailed(e.to_string()))?;

        Ok(())
    }

    pub fn refund_unused_gas_fee(
        &mut self,
        signed_tx: &SignedTx,
        used_gas: U256,
        base_fee: U256,
    ) -> Result<()> {
        let refund_gas = signed_tx.gas_limit().checked_sub(used_gas).ok_or_else(|| {
            BackendError::RefundUnusedGasFailed(String::from(
                "failed checked sub used gas with gas limit",
            ))
        })?;
        let refund_amount = calculate_gas_fee(signed_tx, refund_gas, base_fee)?;

        debug!(target: "backend", "[refund_unused_gas_fee] Refunding {:#x} to {:#x}", refund_amount, signed_tx.sender);

        let basic = self.basic(signed_tx.sender);
        let balance = basic.balance.checked_add(refund_amount).ok_or_else(|| {
            BackendError::RefundUnusedGasFailed(String::from(
                "failed checked add refund amount with account balance",
            ))
        })?;

        let new_basic = Basic { balance, ..basic };

        self.apply(signed_tx.sender, Some(new_basic), None, Vec::new(), false)
            .map_err(|e| BackendError::RefundUnusedGasFailed(e.to_string()))?;

        Ok(())
    }
}

pub struct EVMBackend<'a> {
    storage_backend: Arc<RwLock<TrieBackend>>,
    state: Trie<'a>,
    storage: Arc<Storage>,
    pub vicinity: Vicinity,
}

impl<'a> EVMBackend<'a> {
    pub fn from_root(
        storage_backend: Arc<RwLock<TrieBackend>>,
        state: Trie<'a>,
        storage: Arc<Storage>,
        vicinity: Vicinity,
    ) -> Self {
        EVMBackend {
            storage_backend,
            state,
            storage,
            vicinity,
        }
    }

    pub fn with_read_storage_and_root<T, F: FnOnce(&Trie) -> T>(&self, root: &TrieRoot, f: F) -> T {
        let back = self.storage_backend.read();
        let trie = Trie::new(&*back, &root);
        f(&trie)
    }

    pub fn get_account(&self, address: &H160) -> Option<Account> {
        self.state
            .get(&address.as_bytes())
            .unwrap_or(None)
            .and_then(|addr| Account::decode(&Rlp::new(addr.as_bytes_ref())).ok())
    }

    pub fn get_nonce(&self, address: &H160) -> U256 {
        self.get_account(address)
            .map(|acc| acc.nonce)
            .unwrap_or_default()
    }

    pub fn get_balance(&self, address: &H160) -> U256 {
        self.get_account(address)
            .map(|acc| acc.balance)
            .unwrap_or_default()
    }

    pub fn get_contract_storage(&self, contract: H160, storage_index: &[u8]) -> Result<U256> {
        let Some(account) = self.get_account(&contract) else {
            println!("Not finding contract");
            return Ok(U256::zero());
        };

        self.with_read_storage_and_root(&account.storage_root, |trie| {
            println!("Trying to fetch account storage");
            let storage = U256::from(trie.get(storage_index)?.unwrap_or_default().as_slice());
            println!("storage : {:?}", storage);
            Ok(storage)
        })
    }
}

impl Backend for EVMBackend<'_> {
    fn gas_price(&self) -> U256 {
        trace!(target: "backend", "[EVMBackendMut] Getting gas price");
        self.vicinity.gas_price
    }

    fn origin(&self) -> H160 {
        trace!(target: "backend", "[EVMBackendMut] Getting origin");
        self.vicinity.origin
    }

    fn block_hash(&self, number: U256) -> H256 {
        trace!(target: "backend", "[EVMBackendMut] Getting block hash for block {:x?}", number);
        self.storage
            .get_block_by_number(&number)
            .expect("Could not get block by number")
            .map_or(H256::zero(), |block| block.header.hash())
    }

    fn block_number(&self) -> U256 {
        trace!(target: "backend", "[EVMBackendMut] Getting current block number");
        self.vicinity.block_number
    }

    fn block_coinbase(&self) -> H160 {
        self.vicinity.beneficiary
    }

    fn block_timestamp(&self) -> U256 {
        self.vicinity.timestamp
    }

    fn block_difficulty(&self) -> U256 {
        self.vicinity.block_difficulty
    }

    fn block_randomness(&self) -> Option<H256> {
        self.vicinity.block_randomness
    }

    fn block_gas_limit(&self) -> U256 {
        self.vicinity.block_gas_limit
    }

    fn block_base_fee_per_gas(&self) -> U256 {
        trace!(target: "backend", "[EVMBackendMut] Getting block_base_fee_per_gas");
        self.vicinity.block_base_fee_per_gas
    }

    fn chain_id(&self) -> U256 {
        U256::from(ain_cpp_imports::get_chain_id().expect("Error getting chain_id"))
    }

    fn exists(&self, address: H160) -> bool {
        self.state.contains(&address.as_bytes()).unwrap_or(false)
    }

    fn basic(&self, address: H160) -> Basic {
        trace!(target: "backend", "[EVMBackendMut] basic for address {:x?}", address);
        self.get_account(&address)
            .map(|account| Basic {
                balance: account.balance,
                nonce: account.nonce,
            })
            .unwrap_or_default()
    }

    fn code(&self, address: H160) -> Vec<u8> {
        trace!(target: "backend", "[EVMBackendMut] code for address {:x?}", address);
        self.get_account(&address)
            .and_then(|account| self.storage.get_code_by_hash(account.code_hash).unwrap())
            .unwrap_or_default()
    }

    fn storage(&self, address: H160, index: H256) -> H256 {
        trace!(target: "backend", "[EVMBackendMut] Getting storage for address {:x?} at index {:x?}", address, index);
        self.get_account(&address)
            .and_then(|account| {
                self.with_read_storage_and_root(&account.storage_root, |trie| {
                    println!("Trying to fetch account storage for address {address:?}");
                    let storage =
                        trie.get(&index.as_bytes())
                            .ok()
                            .flatten()
                            .map_or_else(H256::zero, |vec| {
                                println!("vec : {:?}", vec);
                                println!("vec.len() : {:?}", vec.len());
                                H256::from_slice(vec.as_slice())
                            });
                    println!("storage : {:?}", storage);
                    Some(storage)
                })
            })
            .unwrap_or_default()
    }

    fn original_storage(&self, address: H160, index: H256) -> Option<H256> {
        trace!(target: "backend", "[EVMBackendMut] Getting original storage for address {:x?} at index {:x?}", address, index);
        Some(self.storage(address, index))
    }
}

#[derive(Debug)]
pub struct InsufficientBalance {
    pub address: H160,
    pub account_balance: U256,
    pub amount: U256,
}

#[derive(Debug)]
pub enum BackendError {
    TrieCreationFailed(String),
    TrieRestoreFailed(String),
    TrieError(String),
    NoSuchAccount(H160),
    InsufficientBalance(InsufficientBalance),
    DeductPrepayGasFailed(String),
    RefundUnusedGasFailed(String),
}

use std::fmt;

impl fmt::Display for BackendError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BackendError::TrieCreationFailed(e) => {
                write!(f, "BackendError: Failed to create trie {e}")
            }
            BackendError::TrieRestoreFailed(e) => {
                write!(f, "BackendError: Failed to restore trie {e}")
            }
            BackendError::TrieError(e) => write!(f, "BackendError: Trie error {e}"),
            BackendError::NoSuchAccount(address) => {
                write!(f, "BackendError: No such acccount for address {address}")
            }
            BackendError::InsufficientBalance(InsufficientBalance {
                address,
                account_balance,
                amount,
            }) => {
                write!(f, "BackendError: Insufficient balance for address {address}, trying to deduct {amount} but address has only {account_balance}")
            }
            BackendError::DeductPrepayGasFailed(e) => {
                write!(f, "BackendError: Deduct prepay gas failed {e}")
            }
            BackendError::RefundUnusedGasFailed(e) => {
                write!(f, "BackendError: Refund unused gas failed {e}")
            }
        }
    }
}

impl Error for BackendError {}
