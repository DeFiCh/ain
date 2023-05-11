use crate::backend::{EVMBackend, EVMBackendError, Vicinity};
use crate::executor::TxResponse;
use crate::storage::traits::{BlockStorage, PersistentState, PersistentStateError};
use crate::storage::Storage;
use crate::tx_queue::TransactionQueueMap;
use crate::{
    executor::AinExecutor,
    traits::{Executor, ExecutorContext},
    transaction::SignedTx,
};
use anyhow::anyhow;
use ethereum::{AccessList, Account, Log, TransactionV2};
use ethereum_types::{Bloom, BloomInput};

use hex::FromHex;
use log::debug;
use primitive_types::{H160, H256, U256};
use serde::{Deserialize, Serialize};
use std::error::Error;
use std::sync::Arc;
use vsdb_core::vsdb_set_base_dir;
use vsdb_trie_db::MptStore;

pub static TRIE_DB_STORE: &str = "trie_db_store.bin";

pub struct EVMHandler {
    pub tx_queues: Arc<TransactionQueueMap>,
    pub trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
}

#[derive(Serialize, Deserialize)]
pub struct TrieDBStore {
    pub trie_db: MptStore,
}

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
}

impl PersistentState for TrieDBStore {}

impl EVMHandler {
    pub fn new(storage: Arc<Storage>) -> Self {
        let datadir = ain_cpp_imports::get_datadir().expect("Could not get imported datadir");
        let vsdb_dir = format!("{datadir}/.vsdb");
        vsdb_set_base_dir(&vsdb_dir).expect("Could not update vsdb base dir");
        debug!("VSDB dir : {}", vsdb_dir);

        Self {
            tx_queues: Arc::new(TransactionQueueMap::new()),
            trie_store: Arc::new(
                TrieDBStore::load_from_disk(TRIE_DB_STORE).expect("Error loading trie db store"),
            ),
            storage,
        }
    }

    pub fn flush(&self) -> Result<(), PersistentStateError> {
        self.trie_store.save_to_disk(TRIE_DB_STORE)
    }

    pub fn call(
        &self,
        caller: Option<H160>,
        to: Option<H160>,
        value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
    ) -> Result<TxResponse, Box<dyn Error>> {
        let state_root = self
            .storage
            .get_latest_block()
            .map(|block| block.header.state_root)
            .unwrap_or_default();
        println!("state_root : {:#?}", state_root);
        let vicinity = Vicinity {};
        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )
        .map_err(|e| anyhow!("------ Could not restore backend {}", e))?;
        Ok(AinExecutor::new(&mut backend).call(
            ExecutorContext {
                caller,
                to,
                value,
                data,
                gas_limit,
                access_list,
            },
            false,
        ))
    }

    pub fn add_balance(
        &self,
        context: u64,
        address: H160,
        value: U256,
    ) -> Result<(), Box<dyn Error>> {
        self.tx_queues.add_balance(context, address, value)?;
        Ok(())
    }

    pub fn sub_balance(
        &self,
        context: u64,
        address: H160,
        value: U256,
    ) -> Result<(), Box<dyn Error>> {
        self.tx_queues
            .sub_balance(context, address, value)
            .map_err(|e| e.into())
    }

    pub fn validate_raw_tx(&self, tx: &str) -> Result<SignedTx, Box<dyn Error>> {
        let buffer = <Vec<u8>>::from_hex(tx)?;
        let tx: TransactionV2 = ethereum::EnvelopedDecodable::decode(&buffer)
            .map_err(|_| anyhow!("Error: decoding raw tx to TransactionV2"))?;

        let block_number = self
            .storage
            .get_latest_block()
            .map(|block| block.header.number)
            .unwrap_or_default();

        debug!("[validate_raw_tx] block_number : {:#?}", block_number);

        let signed_tx: SignedTx = tx.try_into()?;
        let nonce = self
            .get_nonce(signed_tx.sender, block_number)
            .map_err(|e| anyhow!("Error getting nonce {e}"))?;

        debug!(
            "[validate_raw_tx] signed_tx.sender : {:#?}",
            signed_tx.sender
        );
        debug!(
            "[validate_raw_tx] signed_tx nonce : {:#?}",
            signed_tx.nonce()
        );
        debug!("[validate_raw_tx] nonce : {:#?}", nonce);
        if nonce > signed_tx.nonce() {
            return Err(anyhow!(
                "Invalid nonce. Account nonce {}, signed_tx nonce {}",
                nonce,
                signed_tx.nonce()
            )
            .into());
        }

        // TODO validate balance to pay gas
        // if account.balance < MIN_GAS {
        //     return Err(anyhow!("Insufficiant balance to pay fees").into());
        // }

        match self.call(
            Some(signed_tx.sender),
            signed_tx.to(),
            signed_tx.value(),
            signed_tx.data(),
            signed_tx.gas_limit().as_u64(),
            signed_tx.access_list(),
        ) {
            Ok(TxResponse { exit_reason, .. }) if exit_reason.is_succeed() => Ok(signed_tx),
            Ok(TxResponse { exit_reason, .. }) => {
                Err(anyhow!("Error calling EVM {:?}", exit_reason).into())
            }
            Err(e) => Err(anyhow!("Error calling EVM {:?}", e).into()),
        }
    }

    pub fn get_context(&self) -> u64 {
        self.tx_queues.get_context()
    }

    pub fn discard_context(&self, context: u64) -> Result<(), Box<dyn Error>> {
        self.tx_queues.clear(context)?;
        Ok(())
    }

    pub fn queue_tx(&self, context: u64, raw_tx: &str) -> Result<(), Box<dyn Error>> {
        let signed_tx = self.validate_raw_tx(raw_tx)?;
        self.tx_queues.add_signed_tx(context, signed_tx)?;
        Ok(())
    }

    pub fn logs_bloom(logs: Vec<Log>, bloom: &mut Bloom) {
        for log in logs {
            bloom.accrue(BloomInput::Raw(&log.address[..]));
            for topic in log.topics {
                bloom.accrue(BloomInput::Raw(&topic[..]));
            }
        }
    }
}

impl EVMHandler {
    pub fn get_account(
        &self,
        address: H160,
        block_number: U256,
    ) -> Result<Option<Account>, EVMError> {
        let state_root = self
            .storage
            .get_block_by_number(&block_number)
            .or_else(|| self.storage.get_latest_block())
            .map(|block| block.header.state_root)
            .unwrap_or_default();
        let vicinity = Vicinity {};

        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )?;
        Ok(backend.get_account(address))
    }

    pub fn get_code(&self, address: H160, block_number: U256) -> Result<Option<Vec<u8>>, EVMError> {
        self.get_account(address, block_number).map(|opt_account| {
            opt_account.map_or_else(
                || None,
                |account| self.storage.get_code_by_hash(account.code_hash),
            )
        })
    }

    pub fn get_storage_at(
        &self,
        address: H160,
        position: U256,
        block_number: U256,
    ) -> Result<Option<Vec<u8>>, EVMError> {
        self.get_account(address, block_number)?
            .map_or(Ok(None), |account| {
                let storage_trie = self
                    .trie_store
                    .trie_db
                    .trie_restore(address.as_bytes(), None, account.storage_root.into())
                    .unwrap();

                let tmp: &mut [u8; 32] = &mut [0; 32];
                position.to_big_endian(tmp);
                storage_trie
                    .get(tmp.as_slice())
                    .map_err(|e| EVMError::TrieError(format!("{e}")))
            })
    }

    pub fn get_balance(&self, address: H160, block_number: U256) -> Result<U256, EVMError> {
        let balance = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.balance);

        debug!("Account {} balance {}", address, block_number);
        Ok(balance)
    }

    pub fn get_nonce(&self, address: H160, block_number: U256) -> Result<U256, EVMError> {
        let nonce = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.nonce);

        debug!("Account {} nonce {}", address, nonce);
        Ok(nonce)
    }
}

use std::fmt;
#[derive(Debug)]
pub enum EVMError {
    BackendError(EVMBackendError),
    NoSuchAccount(H160),
    TrieError(String),
}

impl fmt::Display for EVMError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            EVMError::BackendError(e) => write!(f, "EVMError: Backend error: {e}"),
            EVMError::NoSuchAccount(address) => {
                write!(f, "EVMError: No such acccount for address {address:#x}")
            }
            EVMError::TrieError(e) => {
                write!(f, "EVMError: Trie error {e}")
            }
        }
    }
}

impl From<EVMBackendError> for EVMError {
    fn from(e: EVMBackendError) -> Self {
        EVMError::BackendError(e)
    }
}

impl std::error::Error for EVMError {}
