use std::{
    collections::{HashMap, HashSet},
    error::Error,
    sync::Arc,
};

use anyhow::format_err;
use ethereum::{Account, Header, Log};
use ethereum_types::{H160, H256, U256};
use evm::backend::{Apply, ApplyBackend, Backend, Basic};
use hash_db::Hasher as _;
use log::trace;
use rlp::{Decodable, Encodable, Rlp};
use sp_core::{hexdisplay::AsBytesRef, Blake2Hasher};
use vsdb_trie_db::{MptOnce, MptRo};

use crate::{
    fee::calculate_gas_fee,
    storage::{traits::BlockStorage, Storage},
    transaction::SignedTx,
    trie::{TrieDBStore, GENESIS_STATE_ROOT},
    Result,
};

type Hasher = Blake2Hasher;

fn is_empty_account(account: &Account) -> bool {
    account.balance.is_zero() && account.nonce.is_zero() && account.code_hash.is_zero()
}

#[derive(Default, Debug, Clone)]
pub struct Vicinity {
    pub origin: H160,
    pub gas_price: U256,
    pub total_gas_used: U256,
    pub beneficiary: H160,
    pub block_number: U256,
    pub timestamp: u64,
    pub block_difficulty: U256,
    pub block_gas_limit: U256,
    pub block_base_fee_per_gas: U256,
    pub block_randomness: Option<H256>,
}

impl From<Header> for Vicinity {
    fn from(header: Header) -> Self {
        Vicinity {
            beneficiary: header.beneficiary,
            block_number: header.number,
            timestamp: header.timestamp,
            block_difficulty: header.difficulty,
            block_gas_limit: header.gas_limit,
            block_base_fee_per_gas: header.base_fee,
            block_randomness: None,
            ..Default::default()
        }
    }
}

#[derive(Debug, Clone)]
struct OverlayData {
    account: Account,
    code: Option<Vec<u8>>,
    storage: HashMap<H256, H256>,
}

#[derive(Debug, Clone, Default)]
pub struct Overlay {
    state: HashMap<H160, OverlayData>,
    changeset: Vec<HashMap<H160, OverlayData>>,
    deletes: HashSet<H160>,
    creates: HashSet<H160>,
}

impl Overlay {
    pub fn new() -> Self {
        Self {
            state: HashMap::new(),
            changeset: Vec::new(),
            deletes: HashSet::new(),
            creates: HashSet::new(),
        }
    }

    pub fn apply(
        &mut self,
        address: H160,
        account: Account,
        code: Option<Vec<u8>>,
        mut storage: HashMap<H256, H256>,
        reset_storage: bool,
    ) {
        if !reset_storage {
            if let Some(existing_storage) = self.storage(&address) {
                for (k, v) in existing_storage {
                    storage.entry(*k).or_insert_with(|| *v);
                }
            }
        }

        let data = OverlayData {
            account,
            code: code.or(self.get_code(&address)),
            storage,
        };
        self.state.insert(address, data.clone());
    }

    fn mark_delete(&mut self, address: H160) {
        self.deletes.insert(address);
    }

    fn mark_create(&mut self, address: H160) {
        self.creates.insert(address);
    }

    // Keeps track of the number of TXs in the changeset.
    // Should be called after a TX has been fully processed.
    fn inc(&mut self) {
        self.changeset.push(self.state.clone());
    }

    pub fn storage(&self, address: &H160) -> Option<&HashMap<H256, H256>> {
        self.state.get(address).map(|d| &d.storage)
    }

    pub fn storage_val(&self, address: &H160, index: &H256) -> Option<H256> {
        self.storage(address).and_then(|d| d.get(index).cloned())
    }

    pub fn get_account(&self, address: &H160) -> Option<Account> {
        self.state.get(address).map(|d| d.account.to_owned())
    }

    pub fn get_code(&self, address: &H160) -> Option<Vec<u8>> {
        self.state.get(address).and_then(|d| d.code.clone())
    }
}

pub struct EVMBackend {
    state: MptOnce,
    trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
    pub vicinity: Vicinity,
    overlay: Overlay,
}

impl EVMBackend {
    pub fn from_root(
        state_root: H256,
        trie_store: Arc<TrieDBStore>,
        storage: Arc<Storage>,
        vicinity: Vicinity,
        overlay: Option<Overlay>,
    ) -> Result<Self> {
        let state = trie_store
            .trie_db
            .trie_restore(&[0], None, state_root.into())
            .map_err(|e| BackendError::TrieRestoreFailed(e.to_string()))?;

        Ok(EVMBackend {
            state,
            trie_store,
            storage,
            vicinity,
            overlay: overlay.unwrap_or_default(),
        })
    }

    pub fn increase_tx_count(&mut self) {
        self.overlay.inc()
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

        if reset_storage || is_empty_account(&account) {
            self.overlay.mark_create(address);
        }

        if let Some(code) = &code {
            account.code_hash = Hasher::hash(code);
        }

        if let Some(basic) = basic {
            account.balance = basic.balance;
            account.nonce = basic.nonce;
        }

        self.overlay.apply(
            address,
            account.clone(),
            code,
            storage.into_iter().collect(),
            reset_storage,
        );
        Ok(account)
    }

    pub fn clear_overlay(&mut self) {
        self.overlay.state.clear()
    }

    pub fn reset_to_last_changeset(&mut self) {
        self.overlay.state = self.overlay.changeset.last().cloned().unwrap_or_default();
    }

    pub fn reset_from_tx(&mut self, index: usize) {
        self.overlay.state = self
            .overlay
            .changeset
            .get(index)
            .cloned()
            .unwrap_or_default();
        self.overlay.changeset.truncate(index + 1);
    }

    fn apply_overlay(&mut self, is_miner: bool) -> Result<()> {
        for (
            address,
            OverlayData {
                ref mut account,
                code,
                storage,
            },
        ) in self.overlay.state.drain()
        {
            if self.overlay.creates.contains(&address) {
                trace!("Creating trie for {address:x}");
                self.trie_store
                    .trie_db
                    .trie_create(address.as_bytes(), None, true)
                    .map_err(|e| BackendError::TrieCreationFailed(e.to_string()))?;
                account.storage_root = GENESIS_STATE_ROOT;
            }

            if self.overlay.deletes.contains(&address) {
                self.state
                    .remove(address.as_bytes())
                    .expect("Error removing address in state");

                if !is_miner {
                    self.trie_store.trie_db.trie_remove(address.as_bytes());
                }

                continue;
            }

            if !storage.is_empty() {
                let mut storage_trie = self
                    .trie_store
                    .trie_db
                    .trie_restore(address.as_bytes(), None, account.storage_root.into())
                    .map_err(|e| BackendError::TrieRestoreFailed(e.to_string()))?;

                storage.into_iter().for_each(|(k, v)| {
                    trace!(
                        "Apply::Modify storage {address:?}, key: {:x} value: {:x}",
                        k,
                        v
                    );
                    let _ = storage_trie.insert(k.as_bytes(), v.as_bytes());
                });
                account.storage_root = storage_trie.commit().into();
            }

            if let Some(code) = code {
                self.storage.put_code(
                    self.vicinity.block_number,
                    address,
                    account.code_hash,
                    code,
                )?;
            }

            self.state
                .insert(address.as_bytes(), account.rlp_bytes().as_ref())
                .map_err(|e| BackendError::TrieError(format!("{e}")))?;
        }
        Ok(())
    }

    pub fn commit(&mut self, is_miner: bool) -> Result<H256> {
        self.apply_overlay(is_miner)?;
        Ok(self.state.commit().into())
    }

    pub fn update_vicinity_from_tx(&mut self, tx: &SignedTx) -> Result<()> {
        self.vicinity = Vicinity {
            origin: tx.sender,
            gas_price: tx.effective_gas_price(self.block_base_fee_per_gas())?,
            ..self.vicinity
        };
        Ok(())
    }

    pub fn update_vicinity_from_header(&mut self, header: Header) {
        self.vicinity = Vicinity::from(header);
    }

    pub fn update_vicinity_with_gas_used(&mut self, gas_used: U256) {
        self.vicinity = Vicinity {
            total_gas_used: gas_used,
            ..self.vicinity
        };
    }

    // Read-only handle
    pub fn ro_handle(&self) -> MptRo {
        let root = self.state.root();
        self.state.ro_handle(root)
    }

    pub fn deduct_prepay_gas_fee(&mut self, sender: H160, prepay_fee: U256) -> Result<()> {
        trace!(target: "backend", "[deduct_prepay_gas_fee] Deducting {:#x} from {:#x}", prepay_fee, sender);

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

        trace!(target: "backend", "[refund_unused_gas_fee] Refunding {:#x} to {:#x}", refund_amount, signed_tx.sender);

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

impl EVMBackend {
    pub fn get_account(&self, address: &H160) -> Option<Account> {
        self.overlay.get_account(address).or_else(|| {
            self.state
                .get(address.as_bytes())
                .unwrap_or(None)
                .and_then(|addr| Account::decode(&Rlp::new(addr.as_bytes_ref())).ok())
        })
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

    pub fn get_contract_storage(&self, contract: H160, index: H256) -> Result<U256> {
        Ok(U256::from(self.storage(contract, index).as_bytes()))
    }

    pub fn deploy_contract(
        &mut self,
        address: &H160,
        code: Vec<u8>,
        storage: Vec<(H256, H256)>,
    ) -> Result<()> {
        self.apply(*address, None, Some(code), storage, true)?;

        Ok(())
    }

    pub fn update_storage(&mut self, address: &H160, storage: Vec<(H256, H256)>) -> Result<()> {
        self.apply(*address, None, None, storage, false)?;

        Ok(())
    }
}

impl Backend for EVMBackend {
    fn gas_price(&self) -> U256 {
        trace!(target: "backend", "[EVMBackend] Getting gas price");
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
            .expect("Could not get block by number")
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
        U256::from(self.vicinity.timestamp)
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
        trace!(target: "backend", "[EVMBackend] Getting block_base_fee_per_gas");
        self.vicinity.block_base_fee_per_gas
    }

    fn chain_id(&self) -> U256 {
        U256::from(ain_cpp_imports::get_chain_id().expect("Error getting chain_id"))
    }

    fn exists(&self, address: H160) -> bool {
        self.get_account(&address).is_some()
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
        self.overlay.get_code(&address).unwrap_or_else(|| {
            self.get_account(&address)
                .and_then(|account| {
                    self.storage
                        .get_code_by_hash(address, account.code_hash)
                        .ok()
                        .flatten()
                })
                .unwrap_or_default()
        })
    }

    fn storage(&self, address: H160, index: H256) -> H256 {
        trace!(target: "backend", "[EVMBackend] Getting storage for address {:x?} at index {:x?}", address, index);
        let Some(account) = self.get_account(&address) else {
            return H256::zero();
        };

        self.overlay
            .storage_val(&address, &index)
            .unwrap_or_else(|| {
                self.trie_store
                    .trie_db
                    .trie_restore(address.as_ref(), None, account.storage_root.into())
                    .ok()
                    .and_then(|trie| trie.get(index.as_bytes()).ok().flatten())
                    .map(|res| H256::from_slice(res.as_ref()))
                    .unwrap_or_default()
            })
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
                    trace!(
                        "Apply::Modify address {:x}, basic {:?}, code {:?}",
                        address,
                        basic,
                        code,
                    );

                    let new_account = self
                        .apply(address, Some(basic), code, storage, reset_storage)
                        .expect("Error applying state");

                    if is_empty_account(&new_account) && delete_empty {
                        trace!("Deleting empty address {:x?}", address);
                        self.overlay.mark_delete(address);
                    }
                }
                Apply::Delete { address } => {
                    trace!("Deleting address {:x?}", address);
                    self.apply(address, None, None, vec![], false)
                        .expect("Error applying state");
                    self.overlay.mark_delete(address);
                }
            }
        }
    }
}

impl EVMBackend {
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
                write!(f, "BackendError: No such account for address {address}")
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
