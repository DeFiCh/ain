use ain_evm::transaction::SignedTx;
use anyhow::anyhow;
use ethereum::{AccessList, TransactionAction, TransactionV2};
use evm::{
    backend::{Basic, MemoryAccount, MemoryBackend, MemoryVicinity},
    executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata},
    ExitReason, Memory,
};
use hex::FromHex;
use primitive_types::{H160, H256, U256};
use std::error::Error;
use std::sync::{Arc, RwLock};
use std::{collections::BTreeMap, sync::Mutex};

use crate::traits::PersistentState;
use crate::{EVMState, CONFIG, EVM_STATE_PATH, GAS_LIMIT};

#[derive(Clone, Debug)]
pub struct EVMHandler {
    pub state: Arc<RwLock<EVMState>>,
    pub tx_queue: Arc<Mutex<Vec<SignedTx>>>,
}

impl EVMHandler {
    pub fn new() -> Self {
        Self {
            state: Arc::new(RwLock::new(
                EVMState::load_from_disk(EVM_STATE_PATH).unwrap(),
            )),
            tx_queue: Arc::new(Mutex::new(Vec::new())),
        }
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
        let vicinity = MemoryVicinity {
            gas_price: U256::zero(),
            origin: caller.unwrap_or_default(),
            block_hashes: Vec::new(),
            block_number: Default::default(),
            block_coinbase: Default::default(),
            block_timestamp: Default::default(),
            block_difficulty: Default::default(),
            block_gas_limit: Default::default(),
            chain_id: U256::one(),
            block_base_fee_per_gas: U256::zero(),
        };
        let state = self.state.read().unwrap().clone();
        let backend = MemoryBackend::new(&vicinity, state);

        let metadata = StackSubstateMetadata::new(GAS_LIMIT, &CONFIG);
        let state = MemoryStackState::new(metadata, &backend);
        let precompiles = BTreeMap::new(); // TODO Add precompile crate
        let mut executor = StackExecutor::new_with_precompiles(state, &CONFIG, &precompiles);
        let access_list = access_list
            .into_iter()
            .map(|x| (x.address, x.storage_keys))
            .collect::<Vec<_>>();
        match to {
            Some(address) => executor.transact_call(
                caller.unwrap_or_default(),
                address,
                value,
                data.to_vec(),
                gas_limit,
                access_list.into(),
            ),
            None => executor.transact_create(
                caller.unwrap_or_default(),
                value,
                data.to_vec(),
                gas_limit,
                access_list,
            ),
        }
    }

    // TODO wrap in EVM transaction and dryrun with evm_call
    pub fn add_balance(&self, address: &str, value: i64) -> Result<(), Box<dyn Error>> {
        let to = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.entry(to).or_default();
        account.balance = account.balance + value;
        Ok(())
    }

    pub fn sub_balance(&self, address: &str, value: i64) -> Result<(), Box<dyn Error>> {
        let address = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.get_mut(&address).unwrap();
        if account.balance > value.into() {
            account.balance = account.balance - value;
        }
        Ok(())
    }

    fn get_account(&self, address: &H160) -> MemoryAccount {
        self.state
            .read()
            .unwrap()
            .get(&address)
            .map(|account| account.clone())
            .unwrap_or_else(|| MemoryAccount {
                nonce: U256::default(),
                balance: U256::default(),
                storage: BTreeMap::new(),
                code: Vec::new(),
            })
    }

    pub fn validate_raw_tx(&self, tx: &str) -> Result<(), Box<dyn Error>> {
        let buffer = <Vec<u8>>::from_hex(tx)?;
        let tx: TransactionV2 = ethereum::EnvelopedDecodable::decode(&buffer)
            .map_err(|_| anyhow!("Error: decoding raw tx to TransactionV2"))?;

        // TODO Validate gas limit and chain_id

        let sign_tx: SignedTx = tx.try_into()?;

        // TODO validate account nonce and balance to pay gas
        // let account = self.get_account(&sign_tx.sender);
        // if account.nonce >= sign_tx.nonce() {
        //     return Err(anyhow!("Invalid nonce").into());
        // }
        // if account.balance < MIN_GAS {
        //     return Err(anyhow!("Insufficiant balance to pay fees").into());
        // }

        match self.call(
            Some(sign_tx.sender),
            sign_tx.to(),
            sign_tx.value(),
            sign_tx.data(),
            sign_tx.gas_limit().as_u64(),
            sign_tx.access_list(),
        ) {
            (exit_reason, _) if exit_reason.is_succeed() => Ok(()),
            _ => Err(anyhow!("Error calling EVM").into()),
        }
    }
}
