use crate::backend::{EVMBackend, EVMBackendError, InsufficientBalance, Vicinity};
use crate::block::INITIAL_BASE_FEE;
use crate::executor::TxResponse;
use crate::fee::calculate_prepay_gas_fee;
use crate::gas::{check_tx_intrinsic_gas, MIN_GAS_PER_TX};
use crate::receipt::ReceiptService;
use crate::storage::traits::{BlockStorage, PersistentStateError};
use crate::storage::Storage;
use crate::transaction::bridge::{BalanceUpdate, BridgeTx};
use crate::trie::TrieDBStore;
use crate::txqueue::{QueueError, QueueTx, TransactionQueueMap};
use crate::{
    executor::AinExecutor,
    traits::{Executor, ExecutorContext},
    transaction::SignedTx,
};

use ethereum::{AccessList, Account, Block, Log, PartialHeader, TransactionV2};
use ethereum_types::{Bloom, BloomInput, H160, U256};

use anyhow::anyhow;
use hex::FromHex;
use log::debug;
use std::error::Error;
use std::path::PathBuf;
use std::sync::Arc;
use vsdb_core::vsdb_set_base_dir;

pub type NativeTxHash = [u8; 32];

pub const MAX_GAS_PER_BLOCK: U256 = U256([30_000_000, 0, 0, 0]);

pub struct EVMCoreService {
    pub tx_queues: Arc<TransactionQueueMap>,
    pub trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
}
pub struct EthCallArgs<'a> {
    pub caller: Option<H160>,
    pub to: Option<H160>,
    pub value: U256,
    pub data: &'a [u8],
    pub gas_limit: u64,
    pub access_list: AccessList,
    pub block_number: U256,
}

pub struct ValidateTxInfo {
    pub signed_tx: SignedTx,
    pub prepay_fee: U256,
    pub used_gas: u64,
}

fn init_vsdb() {
    debug!(target: "vsdb", "Initializating VSDB");
    let datadir = ain_cpp_imports::get_datadir();
    let path = PathBuf::from(datadir).join("evm");
    if !path.exists() {
        std::fs::create_dir(&path).expect("Error creating `evm` dir");
    }
    let vsdb_dir_path = path.join(".vsdb");
    vsdb_set_base_dir(&vsdb_dir_path).expect("Could not update vsdb base dir");
    debug!(target: "vsdb", "VSDB directory : {}", vsdb_dir_path.display());
}

impl EVMCoreService {
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
        let (state_root, genesis) =
            TrieDBStore::genesis_state_root_from_json(&handler.trie_store, &handler.storage, path)
                .expect("Error getting genesis state root from json");

        let block: Block<TransactionV2> = Block::new(
            PartialHeader {
                state_root,
                number: U256::zero(),
                beneficiary: Default::default(),
                receipts_root: ReceiptService::get_receipts_root(&Vec::new()),
                logs_bloom: Default::default(),
                gas_used: Default::default(),
                gas_limit: genesis.gas_limit.unwrap_or(MAX_GAS_PER_BLOCK),
                extra_data: genesis.extra_data.unwrap_or_default().into(),
                parent_hash: genesis.parent_hash.unwrap_or_default(),
                mix_hash: genesis.mix_hash.unwrap_or_default(),
                nonce: genesis.nonce.unwrap_or_default(),
                timestamp: genesis.timestamp.unwrap_or_default().as_u64(),
                difficulty: genesis.difficulty.unwrap_or_default(),
                base_fee: genesis.base_fee.unwrap_or(INITIAL_BASE_FEE),
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

    pub fn call(&self, arguments: EthCallArgs) -> Result<TxResponse, Box<dyn Error>> {
        let EthCallArgs {
            caller,
            to,
            value,
            data,
            gas_limit,
            access_list,
            block_number,
        } = arguments;

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
        Ok(AinExecutor::new(&mut backend).call(ExecutorContext {
            caller: caller.unwrap_or_default(),
            to,
            value,
            data,
            gas_limit,
            access_list,
        }))
    }

    pub fn validate_raw_tx(
        &self,
        tx: &str,
        queue_id: u64,
        use_context: bool,
    ) -> Result<ValidateTxInfo, Box<dyn Error>> {
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

        // Validate tx nonce
        if nonce > signed_tx.nonce() {
            return Err(anyhow!(
                "Invalid nonce. Account nonce {}, signed_tx nonce {}",
                nonce,
                signed_tx.nonce()
            )
            .into());
        }

        let balance = self
            .get_balance(signed_tx.sender, block_number)
            .map_err(|e| anyhow!("Error getting balance {e}"))?;

        debug!("[validate_raw_tx] Account balance : {:x?}", balance);

        let prepay_fee = calculate_prepay_gas_fee(&signed_tx)?;
        debug!("[validate_raw_tx] prepay_fee : {:x?}", prepay_fee);

        let gas_limit = signed_tx.gas_limit();
        if ain_cpp_imports::past_changi_intermediate_height_4_height() {
            if balance < prepay_fee {
                debug!("[validate_raw_tx] insufficient balance to pay fees");
                return Err(anyhow!("insufficient balance to pay fees").into());
            }

            if ain_cpp_imports::past_changi_intermediate_height_5_height() {
                // Validate tx gas limit with intrinsic gas
                check_tx_intrinsic_gas(&signed_tx)?;
            } else if gas_limit < MIN_GAS_PER_TX {
                debug!("[validate_raw_tx] gas limit is below the minimum gas per tx");
                return Err(anyhow!("gas limit is below the minimum gas per tx").into());
            }
        } else if balance < MIN_GAS_PER_TX || balance < prepay_fee {
            debug!("[validate_raw_tx] insufficient balance to pay fees");
            return Err(anyhow!("insufficient balance to pay fees").into());
        }

        if gas_limit > MAX_GAS_PER_BLOCK {
            debug!("[validate_raw_tx] Gas limit higher than MAX_GAS_PER_BLOCK");
            return Err(anyhow!("Gas limit higher than MAX_GAS_PER_BLOCK").into());
        }

        // Get tx gas usage
        let used_gas = if use_context {
            let TxResponse { used_gas, .. } = self.call(EthCallArgs {
                caller: Some(signed_tx.sender),
                to: signed_tx.to(),
                value: signed_tx.value(),
                data: signed_tx.data(),
                gas_limit: signed_tx.gas_limit().as_u64(),
                access_list: signed_tx.access_list(),
                block_number,
            })?;
            used_gas
        } else {
            u64::default()
        };

        // Validate total gas usage in queued txs exceeds block size
        if use_context {
            debug!("[validate_raw_tx] used_gas: {:#?}", used_gas);
            let total_current_gas_used = self
                .tx_queues
                .get_total_gas_used(queue_id)
                .unwrap_or_default();

            if total_current_gas_used + U256::from(used_gas) > MAX_GAS_PER_BLOCK {
                return Err(anyhow!("Block size limit is more than MAX_GAS_PER_BLOCK").into());
            }
        }

        Ok(ValidateTxInfo {
            signed_tx,
            prepay_fee,
            used_gas,
        })
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

// Transaction queue methods
impl EVMCoreService {
    pub fn add_balance(
        &self,
        queue_id: u64,
        address: H160,
        amount: U256,
        hash: NativeTxHash,
    ) -> Result<(), EVMError> {
        let queue_tx = QueueTx::BridgeTx(BridgeTx::EvmIn(BalanceUpdate { address, amount }));
        self.tx_queues
            .queue_tx(queue_id, queue_tx, hash, U256::zero(), U256::zero())?;
        Ok(())
    }

    pub fn sub_balance(
        &self,
        queue_id: u64,
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
            self.tx_queues
                .queue_tx(queue_id, queue_tx, hash, U256::zero(), U256::zero())?;
            Ok(())
        }
    }

    pub fn get_queue_id(&self) -> u64 {
        self.tx_queues.get_queue_id()
    }

    pub fn clear(&self, queue_id: u64) -> Result<(), EVMError> {
        self.tx_queues.clear(queue_id)?;
        Ok(())
    }

    pub fn remove(&self, queue_id: u64) {
        self.tx_queues.remove(queue_id);
    }

    pub fn remove_txs_by_sender(&self, queue_id: u64, address: H160) -> Result<(), EVMError> {
        self.tx_queues.remove_txs_by_sender(queue_id, address)?;
        Ok(())
    }

    /// Retrieves the next valid nonce for the specified account within a particular queue.
    ///
    /// The method first attempts to retrieve the next valid nonce from the transaction queue associated with the
    /// provided queue_id. If no nonce is found in the transaction queue, that means that no transactions have been
    /// queued for this account in this queue_id. It falls back to retrieving the nonce from the storage at the latest
    /// block. If no nonce is found in the storage (i.e., no transactions for this account have been committed yet),
    /// the nonce is defaulted to zero.
    ///
    /// This method provides a unified view of the nonce for an account, taking into account both transactions that are
    /// waiting to be processed in the queue and transactions that have already been processed and committed to the storage.
    ///
    /// # Arguments
    ///
    /// * `queue_id` - The queue_id queue number.
    /// * `address` - The EVM address of the account whose nonce we want to retrieve.
    ///
    /// # Returns
    ///
    /// Returns the next valid nonce as a `U256`. Defaults to U256::zero()
    pub fn get_next_valid_nonce_in_queue(&self, queue_id: u64, address: H160) -> U256 {
        let nonce = self
            .tx_queues
            .get_next_valid_nonce(queue_id, address)
            .unwrap_or_else(|| {
                let latest_block = self
                    .storage
                    .get_latest_block()
                    .map(|b| b.header.number)
                    .unwrap_or_else(U256::zero);

                self.get_nonce(address, latest_block)
                    .unwrap_or_else(|_| U256::zero())
            });

        debug!(
            "Account {:x?} nonce {:x?} in queue_id {queue_id}",
            address, nonce
        );
        nonce
    }
}

// State methods
impl EVMCoreService {
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
        Ok(backend.get_account(&address))
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

    pub fn get_latest_block_backend(&self) -> Result<EVMBackend, EVMBackendError> {
        let (state_root, block_number) = self
            .storage
            .get_latest_block()
            .map(|block| (block.header.state_root, block.header.number))
            .unwrap_or_default();

        debug!(
            "[get_latest_block_backend] At block number : {:#x}, state_root : {:#x}",
            block_number, state_root
        );
        EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Default::default(),
        )
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
