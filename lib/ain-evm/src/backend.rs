use ethereum::{Account, Log};
use evm::backend::{Apply, ApplyBackend, Backend, Basic};
use hash_db::Hasher as _;
use log::{debug, trace};
use primitive_types::{H160, H256, U256};
use rlp::{Decodable, Encodable, Rlp};
use sp_core::hexdisplay::AsBytesRef;
use sp_core::Blake2Hasher;
use vsdb_trie_db::{MptOnce, MptRo};

use crate::{
    storage::{traits::BlockStorage, Storage},
    traits::BridgeBackend,
    transaction::SignedTx,
    trie::TrieDBStore,
};

type Hasher = Blake2Hasher;

fn is_empty_account(account: &Account) -> bool {
    account.balance.is_zero() && account.nonce.is_zero() && account.code_hash.is_zero()
}

#[derive(Default, Debug)]
pub struct Vicinity {
    pub gas_price: U256,
    pub origin: H160,
    pub beneficiary: H160,
    pub block_number: U256,
    pub timestamp: U256,
    pub gas_limit: U256,
    pub block_base_fee_per_gas: U256,
    pub block_randomness: Option<H256>,
}

pub struct EVMBackend {
    state: MptOnce,
    trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
    pub vicinity: Vicinity,
}

type Result<T> = std::result::Result<T, EVMBackendError>;

impl EVMBackend {
    pub fn from_root(
        state_root: H256,
        trie_store: Arc<TrieDBStore>,
        storage: Arc<Storage>,
        vicinity: Vicinity,
    ) -> Result<Self> {
        let state = trie_store
            .trie_db
            .trie_restore(&[0], None, state_root.into())
            .map_err(|e| EVMBackendError::TrieRestoreFailed(e.to_string()))?;

        Ok(EVMBackend {
            state,
            trie_store,
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
    ) -> Result<Account> {
        let account = self.get_account(&address).unwrap_or(Account {
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
            debug!("Apply::Modify storage, key: {:x} value: {:x}", k, v);
            let _ = storage_trie.insert(k.as_bytes(), v.as_bytes());
        });

        let code_hash = code.map_or(account.code_hash, |code| {
            let code_hash = Hasher::hash(&code);
            self.storage.put_code(code_hash, code);
            code_hash
        });

        let new_account = Account {
            nonce: basic.nonce,
            balance: basic.balance,
            code_hash,
            storage_root: storage_trie.commit().into(),
        };

        self.state
            .insert(address.as_bytes(), new_account.rlp_bytes().as_ref())
            .map_err(|e| EVMBackendError::TrieError(format!("{e}")))?;

        Ok(new_account)
    }

    pub fn commit(&mut self) -> H256 {
        self.state.commit().into()
    }

    // Read-only state root. Does not commit changes to database
    pub fn root(&self) -> H256 {
        self.state.root().into()
    }

    pub fn update_vicinity_from_tx(&mut self, tx: &SignedTx) {
        self.vicinity = Vicinity {
            origin: tx.sender,
            gas_price: tx.gas_price(),
            gas_limit: tx.gas_limit(),
            ..self.vicinity
        };
    }

    // Read-only handle
    pub fn ro_handle(&self) -> MptRo {
        let root = self.state.root();
        self.state.ro_handle(root)
    }

    pub fn deduct_prepay_gas(&mut self, sender: H160, prepay_gas: U256) {
        debug!(target: "backend", "[deduct_prepay_gas] Deducting {:#x} from {:#x}", prepay_gas, sender);

        let basic = self.basic(sender);
        let balance = basic.balance.saturating_sub(prepay_gas);
        let new_basic = Basic { balance, ..basic };

        self.apply(sender, new_basic, None, Vec::new(), false)
            .expect("Error deducting account balance");
        self.commit();
    }

    pub fn refund_unused_gas(
        &mut self,
        sender: H160,
        gas_limit: U256,
        used_gas: U256,
        gas_price: U256,
    ) {
        let refund_gas = gas_limit.saturating_sub(used_gas);
        let refund_amount = refund_gas.saturating_mul(gas_price);

        debug!(target: "backend", "[refund_unused_gas] Refunding {:#x} to {:#x}", refund_amount, sender);

        let basic = self.basic(sender);
        let new_basic = Basic {
            balance: basic.balance.saturating_add(refund_amount),
            ..basic
        };

        self.apply(sender, new_basic, None, Vec::new(), false)
            .expect("Error refunding account balance");
        self.commit();
    }
}

impl EVMBackend {
    pub fn get_account(&self, address: &H160) -> Option<Account> {
        self.state
            .get(address.as_bytes())
            .unwrap_or(None)
            .and_then(|addr| Account::decode(&Rlp::new(addr.as_bytes_ref())).ok())
    }

    pub fn get_nonce(&self, address: &H160) -> U256 {
        self.get_account(address)
            .map(|acc| acc.nonce)
            .unwrap_or_default()
    }
}

impl Backend for EVMBackend {
    fn gas_price(&self) -> U256 {
        trace!(target: "backend", "[EVMBackend] Getting gas");
        self.vicinity.gas_price
    }

    fn origin(&self) -> H160 {
        trace!(target: "backend", "[EVMBackend] Getting origin");
        self.vicinity.origin
    }

    fn block_hash(&self, number: U256) -> H256 {
        trace!(target: "backend", "[EVMBackend] Getting block hash for block {:x?}", number);
        self.storage
            .get_block_by_number(&number)
            .map_or(H256::zero(), |block| block.header.hash())
    }

    fn block_number(&self) -> U256 {
        trace!(target: "backend", "[EVMBackend] Getting current block number");
        self.vicinity.block_number
    }

    fn block_coinbase(&self) -> H160 {
        self.vicinity.beneficiary
    }

    fn block_timestamp(&self) -> U256 {
        self.vicinity.timestamp
    }

    fn block_difficulty(&self) -> U256 {
        U256::zero()
    }

    fn block_randomness(&self) -> Option<H256> {
        self.vicinity.block_randomness
    }

    fn block_gas_limit(&self) -> U256 {
        self.vicinity.gas_limit
    }

    fn block_base_fee_per_gas(&self) -> U256 {
        trace!(target: "backend", "[EVMBackend] Getting block_base_fee_per_gas");
        self.vicinity.block_base_fee_per_gas
    }

    fn chain_id(&self) -> U256 {
        U256::from(ain_cpp_imports::get_chain_id().expect("Error getting chain_id"))
    }

    fn exists(&self, address: H160) -> bool {
        self.state.contains(address.as_bytes()).unwrap_or(false)
    }

    fn basic(&self, address: H160) -> Basic {
        trace!(target: "backend", "[EVMBackend] basic for address {:x?}", address);
        self.get_account(&address)
            .map(|account| Basic {
                balance: account.balance,
                nonce: account.nonce,
            })
            .unwrap_or_default()
    }

    fn code(&self, address: H160) -> Vec<u8> {
        trace!(target: "backend", "[EVMBackend] code for address {:x?}", address);
        self.get_account(&address)
            .and_then(|account| self.storage.get_code_by_hash(account.code_hash))
            .unwrap_or_default()
    }

    fn storage(&self, address: H160, index: H256) -> H256 {
        trace!(target: "backend", "[EVMBackend] Getting storage for address {:x?} at index {:x?}", address, index);
        self.get_account(&address)
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
        trace!(target: "backend", "[EVMBackend] Getting original storage for address {:x?} at index {:x?}", address, index);
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
        Ok(())
    }

    fn sub_balance(&mut self, address: H160, amount: U256) -> Result<()> {
        let account = self
            .get_account(&address)
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
            Ok(())
        }
    }
}

use std::{error::Error, fmt, sync::Arc};

#[derive(Debug)]
pub struct InsufficientBalance {
    pub address: H160,
    pub account_balance: U256,
    pub amount: U256,
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
                write!(f, "EVMBackendError: Failed to create trie {e}")
            }
            EVMBackendError::TrieRestoreFailed(e) => {
                write!(f, "EVMBackendError: Failed to restore trie {e}")
            }
            EVMBackendError::TrieError(e) => write!(f, "EVMBackendError: Trie error {e}"),
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
                write!(f, "EVMBackendError: Insufficient balance for address {address}, trying to deduct {amount} but address has only {account_balance}")
            }
        }
    }
}

impl Error for EVMBackendError {}
