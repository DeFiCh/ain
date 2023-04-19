use crate::traits::{PersistentState, PersistentStateError};
use crate::tx_queue::TransactionQueueMap;
use crate::{executor::AinExecutor, traits::Executor, transaction::SignedTx};
use anyhow::anyhow;
use ethereum::{AccessList, TransactionV2};
use evm::backend::MemoryAccount;
use evm::{
    backend::{MemoryBackend, MemoryVicinity},
    ExitReason,
};
use hex::FromHex;
use primitive_types::{H160, H256, U256};
use std::collections::BTreeMap;
use std::error::Error;
use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;
use std::sync::{Arc, RwLock};

pub static EVM_STATE_PATH: &str = "evm_state.bin";

pub type EVMState = BTreeMap<H160, MemoryAccount>;

#[derive(Clone, Debug)]
pub struct EVMHandler {
    pub state: Arc<RwLock<EVMState>>,
    pub tx_queues: Arc<TransactionQueueMap>,
}

impl PersistentState for EVMState {
    fn save_to_disk(&self, path: &str) -> Result<(), PersistentStateError> {
        let serialized_state = bincode::serialize(self)?;
        let mut file = File::create(path)?;
        file.write_all(&serialized_state)?;
        Ok(())
    }

    fn load_from_disk(path: &str) -> Result<Self, PersistentStateError> {
        if Path::new(path).exists() {
            let mut file = File::open(path)?;
            let mut data = Vec::new();
            file.read_to_end(&mut data)?;
            let new_state: BTreeMap<H160, MemoryAccount> = bincode::deserialize(&data)?;
            Ok(new_state)
        } else {
            Ok(Self::new())
        }
    }
}

impl Default for EVMHandler {
    fn default() -> Self {
        Self::new()
    }
}

impl EVMHandler {
    pub fn new() -> Self {
        Self {
            state: Arc::new(RwLock::new(
                EVMState::load_from_disk(EVM_STATE_PATH).expect("Error loading state"),
            )),
            tx_queues: Arc::new(TransactionQueueMap::new()),
        }
    }

    pub fn flush(&self) -> Result<(), PersistentStateError> {
        self.state.write().unwrap().save_to_disk(EVM_STATE_PATH)
    }

    pub fn call(
        &self,
        caller: Option<H160>,
        to: Option<H160>,
        value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
    ) -> (ExitReason, Vec<u8>) {
        // TODO Add actual gas, chain_id, block_number from header
        let vicinity = get_vicinity(caller, None);

        let state = self.state.read().unwrap().clone();
        let backend = MemoryBackend::new(&vicinity, state);
        let tx_response =
            AinExecutor::new(backend).call(caller, to, value, data, gas_limit, access_list, false);
        (tx_response.exit_reason, tx_response.data)
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

        // TODO validate account nonce and balance to pay gas
        // let account = self.get_account(&signed_tx.sender);
        // if account.nonce >= signed_tx.nonce() {
        //     return Err(anyhow!("Invalid nonce").into());
        // }
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
            (exit_reason, _) if exit_reason.is_succeed() => Ok(signed_tx),
            _ => Err(anyhow!("Error calling EVM").into()),
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

// TBD refine what vicinity we need. gas_price and origin only ?
pub fn get_vicinity(origin: Option<H160>, gas_price: Option<U256>) -> MemoryVicinity {
    MemoryVicinity {
        gas_price: gas_price.unwrap_or(U256::MAX),
        origin: origin.unwrap_or_default(),
        block_hashes: Vec::new(),
        block_number: Default::default(),
        block_coinbase: Default::default(),
        block_timestamp: Default::default(),
        block_difficulty: Default::default(),
        block_gas_limit: U256::MAX,
        chain_id: U256::one(),
        block_base_fee_per_gas: U256::MAX,
    }
}
