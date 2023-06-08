use crate::backend::{EVMBackend, EVMBackendError, InsufficientBalance, Vicinity};
use crate::executor::TxResponse;
use crate::storage::traits::{BlockStorage, PersistentStateError};
use crate::storage::Storage;
use crate::transaction::bridge::{BalanceUpdate, BridgeTx};
use crate::trie::TrieDBStore;
use crate::tx_queue::{QueueError, QueueTx, TransactionQueueMap};
use crate::{
    executor::AinExecutor,
    traits::{Executor, ExecutorContext},
    transaction::SignedTx,
};
use anyhow::anyhow;
use ethereum::{AccessList, Account, Block, Log, PartialHeader, TransactionV2};
use ethereum_types::{Bloom, BloomInput, H160, U256};

use hex::FromHex;
use log::debug;
use std::error::Error;
use std::path::PathBuf;
use std::sync::Arc;
use vsdb_core::vsdb_set_base_dir;

pub type NativeTxHash = [u8; 32];

pub struct EVMHandler {
    pub tx_queues: Arc<TransactionQueueMap>,
    pub trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
}

fn init_vsdb() {
    debug!(target: "vsdb", "Initializating VSDB");
    let datadir = ain_cpp_imports::get_datadir().expect("Could not get imported datadir");
    let path = PathBuf::from(datadir).join("evm");
    if !path.exists() {
        std::fs::create_dir(&path).expect("Error creating `evm` dir");
    }
    let vsdb_dir_path = path.join(".vsdb");
    vsdb_set_base_dir(&vsdb_dir_path).expect("Could not update vsdb base dir");
    debug!(target: "vsdb", "VSDB directory : {}", vsdb_dir_path.display());
}

impl EVMHandler {
    pub fn restore(storage: Arc<Storage>) -> Self {
        init_vsdb();

        Self {
            tx_queues: Arc::new(TransactionQueueMap::new()),
            trie_store: Arc::new(TrieDBStore::restore()),
            storage,
        }
    }

    pub fn new_from_json(storage: Arc<Storage>, path: PathBuf) -> Self {
        debug!("Loading genesis state from {}", path.display());
        init_vsdb();

        let handler = Self {
            tx_queues: Arc::new(TransactionQueueMap::new()),
            trie_store: Arc::new(TrieDBStore::new()),
            storage: Arc::clone(&storage),
        };
        let state_root =
            TrieDBStore::genesis_state_root_from_json(&handler.trie_store, &handler.storage, path)
                .expect("Error getting genesis state root from json");

        let block: Block<TransactionV2> = Block::new(
            PartialHeader {
                state_root,
                number: U256::zero(),
                parent_hash: Default::default(),
                beneficiary: Default::default(),
                receipts_root: Default::default(),
                logs_bloom: Default::default(),
                difficulty: Default::default(),
                gas_limit: Default::default(),
                gas_used: Default::default(),
                timestamp: Default::default(),
                extra_data: Default::default(),
                mix_hash: Default::default(),
                nonce: Default::default(),
            },
            Vec::new(),
            Vec::new(),
        );
        storage.put_latest_block(Some(&block));
        storage.put_block(&block);

        handler
    }

    pub fn flush(&self) -> Result<(), PersistentStateError> {
        self.trie_store.flush()
    }

    pub fn call(
        &self,
        caller: Option<H160>,
        to: Option<H160>,
        value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
        block_number: U256,
    ) -> Result<TxResponse, Box<dyn Error>> {
        let (state_root, block_number) = self
            .storage
            .get_block_by_number(&block_number)
            .map(|block| (block.header.state_root, block.header.number))
            .unwrap_or_default();
        debug!(
            "Calling EVM at block number : {:#x}, state_root : {:#x}",
            block_number, state_root
        );

        let vicinity = Vicinity {
            block_number,
            origin: caller.unwrap_or_default(),
            gas_limit: U256::from(gas_limit),
            ..Default::default()
        };

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

    pub fn validate_raw_tx(&self, tx: &str) -> Result<SignedTx, Box<dyn Error>> {
        debug!("[validate_raw_tx] raw transaction : {:#?}", tx);
        let buffer = <Vec<u8>>::from_hex(tx)?;
        let tx: TransactionV2 = ethereum::EnvelopedDecodable::decode(&buffer)
            .map_err(|_| anyhow!("Error: decoding raw tx to TransactionV2"))?;
        debug!("[validate_raw_tx] TransactionV2 : {:#?}", tx);

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
        if nonce != signed_tx.nonce() {
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

        Ok(signed_tx)
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
    pub fn queue_tx(&self, context: u64, tx: QueueTx, hash: NativeTxHash) -> Result<(), EVMError> {
        self.tx_queues.queue_tx(context, tx, hash)?;
        Ok(())
    }
    pub fn add_balance(
        &self,
        context: u64,
        address: H160,
        amount: U256,
        hash: NativeTxHash,
    ) -> Result<(), EVMError> {
        let queue_tx = QueueTx::BridgeTx(BridgeTx::EvmIn(BalanceUpdate { address, amount }));
        self.tx_queues.queue_tx(context, queue_tx, hash)?;
        Ok(())
    }

    pub fn sub_balance(
        &self,
        context: u64,
        address: H160,
        amount: U256,
        hash: NativeTxHash,
    ) -> Result<(), EVMError> {
        let block_number = self
            .storage
            .get_latest_block()
            .map_or(U256::default(), |block| block.header.number);
        let balance = self.get_balance(address, block_number)?;
        if balance < amount {
            Err(EVMBackendError::InsufficientBalance(InsufficientBalance {
                address,
                account_balance: balance,
                amount,
            })
            .into())
        } else {
            let queue_tx = QueueTx::BridgeTx(BridgeTx::EvmOut(BalanceUpdate { address, amount }));
            self.tx_queues.queue_tx(context, queue_tx, hash)?;
            Ok(())
        }
    }

    pub fn get_context(&self) -> u64 {
        self.tx_queues.get_context()
    }

    pub fn clear(&self, context: u64) -> Result<(), EVMError> {
        self.tx_queues.clear(context)?;
        Ok(())
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

        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
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

        debug!("Account {:x?} balance {:x?}", address, balance);
        Ok(balance)
    }

    pub fn get_nonce(&self, address: H160, block_number: U256) -> Result<U256, EVMError> {
        let nonce = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.nonce);

        debug!("Account {:x?} nonce {:x?}", address, nonce);
        Ok(nonce)
    }
}

use std::fmt;
#[derive(Debug)]
pub enum EVMError {
    BackendError(EVMBackendError),
    QueueError(QueueError),
    NoSuchAccount(H160),
    TrieError(String),
}

impl fmt::Display for EVMError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            EVMError::BackendError(e) => write!(f, "EVMError: Backend error: {e}"),
            EVMError::QueueError(e) => write!(f, "EVMError: Queue error: {e}"),
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

impl From<QueueError> for EVMError {
    fn from(e: QueueError) -> Self {
        EVMError::QueueError(e)
    }
}

impl std::error::Error for EVMError {}
