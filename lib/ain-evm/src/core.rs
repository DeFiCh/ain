use crate::backend::{EVMBackend, EVMBackendError, InsufficientBalance, Vicinity};
use crate::block::INITIAL_BASE_FEE;
use crate::executor::TxResponse;
use crate::fee::calculate_prepay_gas_fee;
use crate::gas::check_tx_intrinsic_gas;
use crate::receipt::ReceiptService;
use crate::services::SERVICES;
use crate::storage::traits::{BlockStorage, PersistentStateError};
use crate::storage::Storage;
use crate::transaction::system::{BalanceUpdate, SystemTx};
use crate::trie::TrieDBStore;
use crate::blocktemplate::{TemplateError, BlockTx, BlockTemplateMap, BlockTemplate};
use crate::{
    executor::AinExecutor,
    traits::{Executor, ExecutorContext},
    transaction::SignedTx,
};
use primitive_types::H256;

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
    pub templates: Arc<BlockTemplateMap>,
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
            templates: Arc::new(BlockTemplateMap::new()),
            trie_store: Arc::new(TrieDBStore::restore()),
            storage,
        }
    }

    pub fn new_from_json(storage: Arc<Storage>, path: PathBuf) -> Self {
        debug!("Loading genesis state from {}", path.display());
        init_vsdb();

        let handler = Self {
            templates: Arc::new(BlockTemplateMap::new()),
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

    /// Validates a raw tx.
    /// 
    /// The validation checks of the tx before we consider it to be valid are:
    /// 1. Account nonce check: verify that the tx nonce must be more than or equal to the account nonce.
    /// 2. Gas price check: verify that the maximum gas price  is minimally of the block initial base fee.
    /// 3. Account balance check: verify that the account balance must minimally have the tx prepay gas fee.
    /// 4. Intrinsic gas limit check: verify that the tx intrinsic gas is within the tx gas limit.
    /// 5. Gas limit check: verify that the tx gas limit is not higher than the maximum gas per block. 
    /// 
    /// # Arguments
    ///
    /// * `tx` - The raw tx.
    /// * `template_id` - The unique block template number.
    /// 
    /// # Returns
    /// 
    /// Returns the signed transaction and tx prepay gas fees.
    pub fn validate_raw_tx(
        &self,
        tx: &str,
        template_id: u64,
    ) -> Result<ValidateTxInfo, Box<dyn Error>> {
        debug!("[validate_raw_tx] raw transaction : {:#?}", tx);
        let signed_tx = SignedTx::try_from(tx).map_err(|_| anyhow!("Error: decoding raw tx to TransactionV2"))?;
        debug!("[verify_tx_fees] TransactionV2 : {:#?}", signed_tx.transaction);

        let state_root = self
            .templates
            .get_state_root(template_id)
            .ok_or(Err(anyhow!("error getting state root, invalid block template id")))?;
        debug!("[validate_raw_tx] state_root : {:#x}", state_root);

        let nonce = self
            .get_nonce(signed_tx.sender, state_root)
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

        // Validate tx gas price with initial block base fee
        let tx_gas_price = get_tx_max_gas_price(&signed_tx);
        if tx_gas_price < INITIAL_BASE_FEE {
            debug!("[validate_raw_tx] tx gas price is lower than initial block base fee");
            return Err(anyhow!("tx gas price is lower than block base fee").into());
        }

        let balance = self
            .get_balance(signed_tx.sender, state_root)
            .map_err(|e| anyhow!("Error getting balance {e}"))?;
        let prepay_fee = calculate_prepay_gas_fee(&signed_tx)?;
        debug!("[validate_raw_tx] Account balance : {:x?}", balance);
        debug!("[validate_raw_tx] prepay_fee : {:x?}", prepay_fee);

        // Validate tx prepay fees with account balance
        if balance < prepay_fee {
            debug!("[validate_raw_tx] insufficient balance to pay fees");
            return Err(anyhow!("insufficient balance to pay fees").into());
        }

        // Validate tx intrinsic gas
        check_tx_intrinsic_gas(&signed_tx)?;

        // Validate gas limit
        let gas_limit = signed_tx.gas_limit();
        if gas_limit > MAX_GAS_PER_BLOCK {
            debug!("[validate_raw_tx] gas limit higher than MAX_GAS_PER_BLOCK");
            return Err(anyhow!("gas limit higher than MAX_GAS_PER_BLOCK").into());
        }

        Ok(ValidateTxInfo {
            signed_tx,
            prepay_fee,
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

// Block template methods
impl EVMCoreService {
    pub fn add_balance(
        &self,
        template_id: u64,
        address: H160,
        amount: U256,
        hash: NativeTxHash,
    ) -> Result<(), EVMError> {
        let block_tx = BlockTx::SystemTx(SystemTx::EvmIn(BalanceUpdate { address, amount }));
        self.templates
            .add_tx(template_id, block_tx, hash, U256::zero())?;
        Ok(())
    }

    pub fn sub_balance(
        &self,
        template_id: u64,
        address: H160,
        amount: U256,
        hash: NativeTxHash,
    ) -> Result<(), EVMError> {
        let state_root = self
            .templates
            .get_state_root(template_id)
            .ok_or(EVMError::TemplateError((TemplateError::NoSuchID)))?;
        let balance = self.get_balance(address, state_root)?;
        if balance < amount {
            Err(EVMBackendError::InsufficientBalance(InsufficientBalance {
                address,
                account_balance: balance,
                amount,
            })
            .into())
        } else {
            let block_tx = BlockTx::SystemTx(SystemTx::EvmOut(BalanceUpdate { address, amount }));
            self.templates
                .add_tx(template_id, block_tx, hash, U256::zero())?;
            Ok(())
        }
    }

    pub fn get_template_id(
        &self,
        trie_store: Arc<TrieDBStore>,
        storage: Arc<Storage>,
        vicinity: Vicinity,
        state_root: H256,
        block_base_fee: U256,
    ) -> Result<u64> {
        self.templates.get_template_id(trie_store, storage, vicinity, state_root, block_base_fee)
    }

    pub fn clear(&self, template_id: u64) -> Result<(), EVMError> {
        self.templates.clear(template_id)?;
        Ok(())
    }

    pub fn remove(&self, template_id: u64) {
        self.templates.remove(template_id);
    }

    pub fn remove_txs_by_sender(&self, template_id: u64, address: H160) -> Result<(), EVMError> {
        self.templates.remove_txs_by_sender(template_id, address)?;
        Ok(())
    }

    /// Retrieves the next valid nonce for the specified account within a particular template.
    ///
    /// The method first attempts to retrieve the next valid nonce from the block template associated with the provided
    /// template_id. If no nonce is found in the template, that means that no transactions have been included for this
    /// account in this template_id. It falls back to retrieving the nonce from the storage at the latest block. If no
    /// nonce is found in the storage (i.e., no transactions for this account have been committed yet), the nonce is
    /// defaulted to zero.
    ///
    /// This method provides a unified view of the nonce for an account, taking into account both transactions that are
    /// waiting to be processed in the template and transactions that have already been processed and committed to the
    /// storage.
    ///
    /// # Arguments
    ///
    /// * `template_id` - The unique block template number.
    /// * `address` - The EVM address of the account whose nonce we want to retrieve.
    ///
    /// # Returns
    ///
    /// Returns the next valid nonce as a `U256`. Defaults to U256::zero()
    pub fn get_next_valid_nonce_in_template(&self, template_id: u64, address: H160) -> U256 {
        let nonce = self
            .templates
            .get_next_valid_nonce(template_id, address)
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
            "Account {:x?} nonce {:x?} in template_id {template_id}",
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
        state_root: H256,
    ) -> Result<Option<Account>, EVMError> {
        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )?;
        Ok(backend.get_account(&address))
    }

    pub fn get_latest_contract_storage(
        &self,
        contract: H160,
        storage_index: H256,
        state_root: H256,
    ) -> Result<U256, EVMError> {
        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )?;

        backend
            .get_contract_storage(contract, storage_index.as_bytes())
            .map_err(|e| EVMError::TrieError(e.to_string()))
    }

    pub fn get_code(&self, address: H160, state_root: H256) -> Result<Option<Vec<u8>>, EVMError> {
        self.get_account(address, state_root).map(|opt_account| {
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
        state_root: H256,
    ) -> Result<Option<Vec<u8>>, EVMError> {
        self.get_account(address, state_root)?
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

    pub fn get_balance(&self, address: H160, state_root: H256) -> Result<U256, EVMError> {
        let balance = self
            .get_account(address, state_root)?
            .map_or(U256::zero(), |account| account.balance);

        debug!("Account {:x?} balance {:x?}", address, balance);
        Ok(balance)
    }

    pub fn get_nonce(&self, address: H160, state_root: H256) -> Result<U256, EVMError> {
        let nonce: U256 = self
            .get_account(address, state_root)?
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
    TemplateError(TemplateError),
    NoSuchAccount(H160),
    TrieError(String),
}

impl fmt::Display for EVMError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            EVMError::BackendError(e) => write!(f, "EVMError: Backend error: {e}"),
            EVMError::TemplateError(e) => write!(f, "EVMError: Template error: {e}"),
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

impl From<TemplateError> for EVMError {
    fn from(e: TemplateError) -> Self {
        EVMError::TemplateError(e)
    }
}

impl std::error::Error for EVMError {}
