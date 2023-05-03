use crate::storage::traits::{PersistentState, PersistentStateError};
use crate::storage::Storage;
use crate::trie::{StateTrie, TrieBackend, TrieRoot};
use crate::tx_queue::TransactionQueueMap;
use crate::{
    executor::AinExecutor,
    traits::{Executor, ExecutorContext},
    transaction::SignedTx,
};
use anyhow::anyhow;
use ethereum::{AccessList, Log, TransactionV2};
use ethereum_types::{Bloom, BloomInput};
use evm::backend::{ApplyBackend, MemoryAccount};
use evm::{
    backend::{MemoryBackend, MemoryVicinity},
    ExitReason,
};
use hex::FromHex;
use primitive_types::{H160, H256, U256};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::error::Error;
use std::sync::{Arc, RwLock};
use vsdb_trie_db::MptStore;

pub static EVM_STATE_FILE: &str = "evm_state.bin";
pub static TRIE_DB_STORE: &str = "trie_db_store.bin";

pub type EVMState = BTreeMap<H160, MemoryAccount>;

pub struct EVMHandler {
    pub state: Arc<RwLock<EVMState>>,
    pub tx_queues: Arc<TransactionQueueMap>,
    trie_db: Arc<RwLock<TrieDBStore>>,
    storage: Arc<Storage>,
}

#[derive(Serialize, Deserialize)]
struct TrieDBStore {
    trie_db: MptStore,
}

impl Default for TrieDBStore {
    fn default() -> Self {
        Self {
            trie_db: MptStore::new(),
        }
    }
}

impl PersistentState for EVMState {}
impl PersistentState for TrieDBStore {}

impl EVMHandler {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self {
            state: Arc::new(RwLock::new(
                EVMState::load_from_disk(EVM_STATE_FILE).expect("Error loading state"),
            )),
            tx_queues: Arc::new(TransactionQueueMap::new()),
            trie_db: Arc::new(RwLock::new(
                TrieDBStore::load_from_disk(TRIE_DB_STORE).expect("Error loading trie db store"),
            )),
            storage,
        }
    }

    pub fn flush(&self) -> Result<(), PersistentStateError> {
        self.state.write().unwrap().save_to_disk(EVM_STATE_FILE)?;
        self.trie_db.write().unwrap().save_to_disk(TRIE_DB_STORE)
    }

    pub fn call(
        &self,
        caller: Option<H160>,
        to: Option<H160>,
        value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
    ) -> (ExitReason, Vec<u8>, u64) {
        // TODO Add actual gas, chain_id, block_number from header
        let vicinity = get_vicinity(caller, None);

        let state = self.state.read().unwrap().clone();
        let backend = MemoryBackend::new(&vicinity, state);
        let tx_response = AinExecutor::new(backend).call(
            ExecutorContext {
                caller,
                to,
                value,
                data,
                gas_limit,
                access_list,
            },
            false,
        );
        (
            tx_response.exit_reason,
            tx_response.data,
            tx_response.used_gas,
        )
    }

    // TODO wrap in EVM transaction and dryrun with evm_call
    pub fn add_balance(&self, context: u64, address: H160, value: U256) {
        self.tx_queues.add_balance(context, address, value)
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

        // TODO Validate gas limit and chain_id

        let signed_tx: SignedTx = tx.try_into()?;
        let account = self.get_account(signed_tx.sender);
        if account.nonce > signed_tx.nonce() {
            return Err(anyhow!("Invalid nonce").into());
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
            (exit_reason, _, _) if exit_reason.is_succeed() => Ok(signed_tx),
            (exit_reason, _, _) => Err(anyhow!("Error calling EVM {:?}", exit_reason).into()),
        }
    }

    pub fn get_context(&self) -> u64 {
        let state = self.state.read().unwrap().clone();
        self.tx_queues.get_context(state)
    }

    pub fn discard_context(&self, context: u64) {
        self.tx_queues.clear(context)
    }

    pub fn queue_tx(&self, context: u64, raw_tx: &str) -> Result<(), Box<dyn Error>> {
        let signed_tx = self.validate_raw_tx(raw_tx)?;
        self.tx_queues.add_signed_tx(context, signed_tx);
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
    pub fn get_account(&self, account: H160) -> MemoryAccount {
        self.state
            .read()
            .unwrap()
            .get(&account)
            .unwrap_or(&Default::default())
            .to_owned()
    }

    pub fn get_code(&self, account: H160) -> Vec<u8> {
        self.get_account(account).code
    }

    pub fn get_storage(&self, account: H160) -> BTreeMap<H256, H256> {
        self.get_account(account).storage
    }

    pub fn get_balance(&self, account: H160) -> U256 {
        self.get_account(account).balance
    }
    pub fn get_nonce(&self, account: H160) -> U256 {
        self.get_account(account).nonce
    }
}
