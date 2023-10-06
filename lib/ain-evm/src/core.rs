use std::collections::BTreeMap;
use std::rc::Rc;
use std::{
    collections::{BTreeSet, HashMap},
    path::PathBuf,
    sync::{Arc, Mutex},
};

use crate::fee::calculate_prepay_gas_fee;
use ain_contracts::{get_transferdomain_contract, FixedContract};
use anyhow::format_err;
use ethereum::{AccessList, Account, Block, Log, PartialHeader, TransactionAction, TransactionV2};
use ethereum_types::{Bloom, BloomInput, H160, H256, U256};
use evm::executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata};
use evm::Capture::Exit;
use evm::{Capture, Config};
use evm_runtime::tracing::using;
use log::{debug, trace};
use std::borrow::BorrowMut;
use vsdb_core::vsdb_set_base_dir;

use crate::opcode;
use crate::precompiles::Context;
use crate::{
    backend::{BackendError, EVMBackend, Vicinity},
    block::INITIAL_BASE_FEE,
    executor::{AinExecutor, ExecutorContext, TxResponse},
    gas::check_tx_intrinsic_gas,
    receipt::ReceiptService,
    storage::{traits::BlockStorage, Storage},
    transaction::SignedTx,
    trie::{TrieDBStore, GENESIS_STATE_ROOT},
    txqueue::TransactionQueueMap,
    weiamount::WeiAmount,
    Result,
};

pub type XHash = String;

#[derive(Clone, Debug)]
pub struct ExecutionStep {
    pub pc: usize,
    pub op: String,
    pub gas: u64,
    pub gas_cost: u64,
    pub stack: Vec<H256>,
    pub memory: Vec<u8>,
}

pub struct EVMCoreService {
    pub tx_queues: Arc<TransactionQueueMap>,
    pub trie_store: Arc<TrieDBStore>,
    storage: Arc<Storage>,
    nonce_store: Mutex<HashMap<H160, BTreeSet<U256>>>,
}
pub struct EthCallArgs<'a> {
    pub caller: Option<H160>,
    pub to: Option<H160>,
    pub value: U256,
    pub data: &'a [u8],
    pub gas_limit: u64,
    pub gas_price: Option<U256>,
    pub max_fee_per_gas: Option<U256>,
    pub access_list: AccessList,
    pub block_number: U256,
    pub transaction_type: Option<U256>,
}

pub struct ValidateTxInfo {
    pub signed_tx: SignedTx,
    pub prepay_fee: U256,
    pub higher_nonce: bool,
}

fn init_vsdb(path: PathBuf) {
    debug!(target: "vsdb", "Initializating VSDB");
    let vsdb_dir_path = path.join(".vsdb");
    vsdb_set_base_dir(&vsdb_dir_path).expect("Could not update vsdb base dir");
    debug!(target: "vsdb", "VSDB directory : {}", vsdb_dir_path.display());
}

impl EVMCoreService {
    pub fn restore(storage: Arc<Storage>, path: PathBuf) -> Self {
        init_vsdb(path);

        Self {
            tx_queues: Arc::new(TransactionQueueMap::new()),
            trie_store: Arc::new(TrieDBStore::restore()),
            storage,
            nonce_store: Mutex::new(HashMap::new()),
        }
    }

    pub fn new_from_json(
        storage: Arc<Storage>,
        genesis_path: PathBuf,
        evm_datadir: PathBuf,
    ) -> Result<Self> {
        debug!("Loading genesis state from {}", genesis_path.display());
        init_vsdb(evm_datadir);

        let handler = Self {
            tx_queues: Arc::new(TransactionQueueMap::new()),
            trie_store: Arc::new(TrieDBStore::new()),
            storage: Arc::clone(&storage),
            nonce_store: Mutex::new(HashMap::new()),
        };
        let (state_root, genesis) = TrieDBStore::genesis_state_root_from_json(
            &handler.trie_store,
            &handler.storage,
            genesis_path,
        )?;

        let gas_limit = storage.get_attributes_or_default()?.block_gas_limit;
        let block: Block<TransactionV2> = Block::new(
            PartialHeader {
                state_root,
                number: U256::zero(),
                beneficiary: H160::default(),
                receipts_root: ReceiptService::get_receipts_root(&Vec::new()),
                logs_bloom: Bloom::default(),
                gas_used: U256::default(),
                gas_limit: genesis.gas_limit.unwrap_or(U256::from(gas_limit)),
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
        storage.put_latest_block(Some(&block))?;
        storage.put_block(&block)?;

        Ok(handler)
    }

    pub fn flush(&self) -> Result<()> {
        self.trie_store.flush()
    }

    pub fn call(&self, arguments: EthCallArgs) -> Result<TxResponse> {
        let EthCallArgs {
            caller,
            to,
            value,
            data,
            gas_limit,
            gas_price,
            max_fee_per_gas,
            access_list,
            block_number,
            transaction_type,
        } = arguments;

        let (state_root, block_number, beneficiary, base_fee, timestamp) = self
            .storage
            .get_block_by_number(&block_number)?
            .map(|block| {
                (
                    block.header.state_root,
                    block.header.number,
                    block.header.beneficiary,
                    block.header.base_fee,
                    block.header.timestamp,
                )
            })
            .unwrap_or_default();
        debug!(
            "Calling EVM at block number : {:#x}, state_root : {:#x}",
            block_number, state_root
        );
        debug!("[call] caller: {:?}", caller);
        let vicinity = Vicinity {
            block_number,
            origin: caller.unwrap_or_default(),
            gas_limit: U256::from(gas_limit),
            gas_price: if transaction_type == Some(U256::from(2)) {
                max_fee_per_gas.unwrap_or_default()
            } else {
                gas_price.unwrap_or_default()
            },
            beneficiary,
            block_base_fee_per_gas: base_fee,
            timestamp: U256::from(timestamp),
            ..Vicinity::default()
        };
        debug!("[call] vicinity: {:?}", vicinity);

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )
        .map_err(|e| format_err!("------ Could not restore backend {}", e))?;

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
    /// The pre-validation checks of the tx before we consider it to be valid are:
    /// 1. Gas price check: verify that the maximum gas price is minimally of the block initial base fee.
    /// 2. Gas price and tx value check: verify that amount is within money range.
    /// 3. Intrinsic gas limit check: verify that the tx intrinsic gas is within the tx gas limit.
    /// 4. Gas limit check: verify that the tx gas limit is not higher than the maximum gas per block.
    /// 5. Account nonce check: verify that the tx nonce must be more than or equal to the account nonce.
    ///
    /// The validation checks with state context of the tx before we consider it to be valid are:
    /// 1. Nonce check: Returns flag if nonce is lower or higher than the current state account nonce.
    /// 2. Execute the tx with the state root from the txqueue.
    /// 3. Account balance check: verify that the account balance must minimally have the tx prepay gas fee.
    /// 4. Check the total gas used in the queue with the addition of the tx do not exceed the block size limit.
    ///
    /// # Arguments
    ///
    /// * `tx` - The raw tx.
    /// * `queue_id` - The queue_id queue number.
    /// * `pre_validate` - Validate the raw tx with or without state context.
    ///
    /// # Returns
    ///
    /// Returns the signed tx, tx prepay gas fees and the gas used to call the tx.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn validate_raw_tx(
        &self,
        tx: &str,
        queue_id: u64,
        pre_validate: bool,
    ) -> Result<ValidateTxInfo> {
        debug!("[validate_raw_tx] queue_id {}", queue_id);
        debug!("[validate_raw_tx] raw transaction : {:#?}", tx);
        let signed_tx = SignedTx::try_from(tx)
            .map_err(|_| format_err!("Error: decoding raw tx to TransactionV2"))?;
        debug!("[validate_raw_tx] signed_tx : {:#?}", signed_tx);

        let state_root = self.tx_queues.get_latest_state_root_in(queue_id)?;
        debug!("[validate_raw_tx] state_root : {:#?}", state_root);

        let mut backend = self.get_backend(state_root)?;

        let signed_tx: SignedTx = tx.try_into()?;
        let nonce = backend.get_nonce(&signed_tx.sender);
        debug!(
            "[validate_raw_tx] signed_tx.sender : {:#?}",
            signed_tx.sender
        );
        debug!(
            "[validate_raw_tx] signed_tx nonce : {:#?}",
            signed_tx.nonce()
        );
        debug!("[validate_raw_tx] nonce : {:#?}", nonce);

        // Validate tx gas price with initial block base fee
        let tx_gas_price = signed_tx.gas_price();
        if tx_gas_price < INITIAL_BASE_FEE {
            debug!("[validate_raw_tx] tx gas price is lower than initial block base fee");
            return Err(format_err!("tx gas price is lower than initial block base fee").into());
        }

        // Validate tx gas price and tx value within money range
        if !WeiAmount(tx_gas_price).wei_range() || !WeiAmount(signed_tx.value()).wei_range() {
            debug!("[validate_raw_tx] value more than money range");
            return Err(format_err!("value more than money range").into());
        }

        // Validate tx gas limit with intrinsic gas
        check_tx_intrinsic_gas(&signed_tx)?;

        // Validate gas limit
        let gas_limit = signed_tx.gas_limit();
        let block_gas_limit = self.storage.get_attributes_or_default()?.block_gas_limit;
        if gas_limit > U256::from(block_gas_limit) {
            debug!("[validate_raw_tx] gas limit higher than max_gas_per_block");
            return Err(format_err!("gas limit higher than max_gas_per_block").into());
        }

        let prepay_fee = calculate_prepay_gas_fee(&signed_tx)?;
        debug!("[validate_raw_tx] prepay_fee : {:x?}", prepay_fee);

        if pre_validate {
            // Validate tx nonce
            if nonce > signed_tx.nonce() {
                return Err(format_err!(
                    "Invalid nonce. Account nonce {}, signed_tx nonce {}",
                    nonce,
                    signed_tx.nonce()
                )
                .into());
            }

            let higher_nonce = nonce < signed_tx.nonce();
            return Ok(ValidateTxInfo {
                signed_tx,
                prepay_fee,
                higher_nonce,
            });
        } else {
            // Validate tx prepay fees with account balance
            let balance = backend.get_balance(&signed_tx.sender);
            debug!("[validate_raw_tx] Account balance : {:x?}", balance);

            if balance < prepay_fee {
                debug!("[validate_raw_tx] insufficient balance to pay fees");
                return Err(format_err!("insufficient balance to pay fees").into());
            }

            // Execute tx
            let mut executor = AinExecutor::new(&mut backend);
            let (tx_response, ..) = executor.exec(&signed_tx, prepay_fee);

            // Validate total gas usage in queued txs exceeds block size
            debug!("[validate_raw_tx] used_gas: {:#?}", tx_response.used_gas);
            let total_current_gas_used = self
                .tx_queues
                .get_total_gas_used_in(queue_id)
                .unwrap_or_default();

            let block_gas_limit = self.storage.get_attributes_or_default()?.block_gas_limit;
            if total_current_gas_used + U256::from(tx_response.used_gas)
                > U256::from(block_gas_limit)
            {
                return Err(format_err!("Tx can't make it in block. Block size limit {}, pending block gas used : {:x?}, tx used gas : {:x?}, total : {:x?}", block_gas_limit, total_current_gas_used, U256::from(tx_response.used_gas), total_current_gas_used + U256::from(tx_response.used_gas)).into());
            }
        }

        Ok(ValidateTxInfo {
            signed_tx,
            prepay_fee,
            higher_nonce: false,
        })
    }

    /// Validates a raw transfer domain tx.
    ///
    /// The validation checks of the tx before we consider it to be valid are:
    /// 1. Account nonce check: verify that the tx nonce must be more than or equal to the account nonce.
    /// 2. tx value check: verify that amount is set to zero.
    /// 3. Verify that transaction action is a call to the transferdomain contract address.
    ///
    /// # Arguments
    ///
    /// * `tx` - The raw tx.
    /// * `queue_id` - The queue_id queue number.
    ///
    /// # Returns
    ///
    /// Returns the validation result.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn validate_raw_transferdomain_tx(&self, tx: &str, queue_id: u64) -> Result<()> {
        debug!("[validate_raw_transferdomain_tx] queue_id {}", queue_id);
        debug!(
            "[validate_raw_transferdomain_tx] raw transaction : {:#?}",
            tx
        );
        let signed_tx = SignedTx::try_from(tx)
            .map_err(|_| format_err!("Error: decoding raw tx to TransactionV2"))?;
        debug!(
            "[validate_raw_transferdomain_tx] signed_tx : {:#?}",
            signed_tx
        );

        let state_root = self.tx_queues.get_latest_state_root_in(queue_id)?;
        debug!(
            "[validate_raw_transferdomain_tx] state_root : {:#?}",
            state_root
        );

        let backend = self.get_backend(state_root)?;

        let signed_tx: SignedTx = tx.try_into()?;
        let nonce = backend.get_nonce(&signed_tx.sender);
        debug!(
            "[validate_raw_transferdomain_tx] signed_tx.sender : {:#?}",
            signed_tx.sender
        );
        debug!(
            "[validate_raw_transferdomain_tx] signed_tx nonce : {:#?}",
            signed_tx.nonce()
        );
        debug!("[validate_raw_transferdomain_tx] nonce : {:#?}", nonce);

        // Validate tx nonce
        if nonce > signed_tx.nonce() {
            return Err(format_err!(
                "Invalid nonce. Account nonce {}, signed_tx nonce {}",
                nonce,
                signed_tx.nonce()
            )
            .into());
        }

        // Validate tx value equal to zero
        if signed_tx.value() != U256::zero() {
            debug!("[validate_raw_transferdomain_tx] value not equal to zero");
            return Err(format_err!("value not equal to zero").into());
        }

        // Verify transaction action and transferdomain contract address
        let FixedContract { fixed_address, .. } = get_transferdomain_contract();
        match signed_tx.action() {
            TransactionAction::Call(address) => {
                if address != fixed_address {
                    return Err(format_err!(
                        "Invalid call address. Fixed address: {:#?}, signed_tx call address: {:#?}",
                        fixed_address,
                        address
                    )
                    .into());
                }
            }
            _ => {
                return Err(
                    format_err!("tx action not a call to transferdomain contract address").into(),
                )
            }
        }

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

// Transaction queue methods
impl EVMCoreService {
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn create_queue(&self) -> Result<u64> {
        let (target_block, initial_state_root) = match self.storage.get_latest_block()? {
            None => (U256::zero(), GENESIS_STATE_ROOT), // Genesis queue
            Some(block) => (block.header.number + 1, block.header.state_root),
        };
        let queue_id = self.tx_queues.create(target_block, initial_state_root);
        Ok(queue_id)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_queue(&self, queue_id: u64) {
        self.tx_queues.remove(queue_id);
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_txs_above_hash_in(
        &self,
        queue_id: u64,
        target_hash: XHash,
    ) -> Result<Vec<XHash>> {
        let hashes = self
            .tx_queues
            .remove_txs_above_hash_in(queue_id, target_hash)?;
        Ok(hashes)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_target_block_in(&self, queue_id: u64) -> Result<U256> {
        let target_block = self.tx_queues.get_target_block_in(queue_id)?;
        Ok(target_block)
    }

    /// Retrieves the next valid nonce for the specified account within a particular queue.
    /// # Arguments
    ///
    /// * `queue_id` - The queue_id queue number.
    /// * `address` - The EVM address of the account whose nonce we want to retrieve.
    ///
    /// # Returns
    ///
    /// Returns the next valid nonce as a `U256`. Defaults to U256::zero()
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_next_valid_nonce_in_queue(
        &self,
        queue_id: u64,
        address: H160,
    ) -> Result<U256> {
        let state_root = self.tx_queues.get_latest_state_root_in(queue_id)?;
        let backend = self.get_backend(state_root)?;
        let nonce = backend.get_nonce(&address);
        trace!(
            "[get_next_valid_nonce_in_queue] Account {address:x?} nonce {nonce:x?} in queue_id {queue_id}"
        );
        Ok(nonce)
    }

    pub fn trace_transaction(
        &self,
        caller: H160,
        to: Option<H160>,
        _value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
        block_number: U256,
    ) -> Result<(Vec<ExecutionStep>, bool, Vec<u8>, u64)> {
        let (state_root, block_number) = self
            .storage
            .get_block_by_number(&block_number)?
            .ok_or_else(|| format_err!("Cannot find block"))
            .map(|block| (block.header.state_root, block.header.number))?;

        debug!(
            "Calling EVM at block number : {:#x}, state_root : {:#x}",
            block_number, state_root
        );

        let vicinity = Vicinity {
            block_number,
            origin: caller,
            gas_limit: U256::from(gas_limit),
            ..Default::default()
        };

        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )
        .map_err(|e| format_err!("Could not restore backend {}", e))?;

        let config = Config::shanghai();
        let metadata = StackSubstateMetadata::new(gas_limit, &config);
        let state = MemoryStackState::new(metadata, &backend);
        let precompiles = BTreeMap::new(); // TODO Add precompile crate
        let mut executor = StackExecutor::new_with_precompiles(state, &config, &precompiles);

        let mut runtime = evm::Runtime::new(
            Rc::new(match to {
                Some(to) => self
                    .get_code(to, block_number)
                    .unwrap_or_default()
                    .unwrap_or_default(),
                None => Vec::new(),
            }),
            Rc::new(data.to_vec()),
            Context {
                caller,
                address: caller,
                apparent_value: U256::default(),
            },
            1024,
            usize::MAX,
        );

        let gasometer = evm::gasometer::Gasometer::new(gas_limit, &config);
        let mut listener = crate::eventlistener::Listener::new(gasometer);

        let (execution_success, return_value) = using(&mut listener, move || {
            runtime.run(&mut executor);

            (runtime.machine().position().clone().err().expect("Execution not completed").is_succeed(), runtime.machine().return_value())
        });

        Ok((
            listener.trace,
            execution_success,
            return_value,
            listener.gasometer.total_used_gas(),
        ))
    }
}

// State methods
impl EVMCoreService {
    pub fn get_state_root(&self) -> Result<H256> {
        let state_root = self
            .storage
            .get_latest_block()?
            .map_or(H256::default(), |block| block.header.state_root);
        Ok(state_root)
    }
    pub fn get_account(&self, address: H160, block_number: U256) -> Result<Option<Account>> {
        let state_root = self
            .storage
            .get_block_by_number(&block_number)?
            .map(|block| block.header.state_root)
            .ok_or(format_err!(
                "[get_account] Block number {:x?} not found",
                block_number
            ))?;

        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )?;
        Ok(backend.get_account(&address))
    }

    pub fn get_latest_contract_storage(&self, contract: H160, storage_index: H256) -> Result<U256> {
        let state_root = self.get_state_root()?;
        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )?;

        backend.get_contract_storage(contract, storage_index.as_bytes())
    }

    pub fn get_code(&self, address: H160, block_number: U256) -> Result<Option<Vec<u8>>> {
        self.get_account(address, block_number)?
            .map_or(Ok(None), |account| {
                self.storage.get_code_by_hash(account.code_hash)
            })
    }

    pub fn get_storage_at(
        &self,
        address: H160,
        position: U256,
        block_number: U256,
    ) -> Result<Option<Vec<u8>>> {
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
                    .map_err(|e| BackendError::TrieError(e.to_string()).into())
            })
    }

    pub fn get_balance(&self, address: H160, block_number: U256) -> Result<U256> {
        let balance = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.balance);

        debug!("Account {:x?} balance {:x?}", address, balance);
        Ok(balance)
    }

    pub fn get_nonce_from_block_number(&self, address: H160, block_number: U256) -> Result<U256> {
        let nonce = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.nonce);

        debug!("Account {:x?} nonce {:x?}", address, nonce);
        Ok(nonce)
    }

    pub fn get_nonce_from_state_root(&self, address: H160, state_root: H256) -> Result<U256> {
        let backend = self.get_backend(state_root)?;
        let nonce = backend.get_nonce(&address);
        Ok(nonce)
    }

    pub fn get_latest_block_backend(&self) -> Result<EVMBackend> {
        let (state_root, block_number) = self
            .storage
            .get_latest_block()?
            .map(|block| (block.header.state_root, block.header.number))
            .unwrap_or_default();

        trace!(
            "[get_latest_block_backend] At block number : {:#x}, state_root : {:#x}",
            block_number,
            state_root
        );
        EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )
    }

    pub fn get_backend(&self, state_root: H256) -> Result<EVMBackend> {
        trace!("[get_backend] state_root : {:#x}", state_root);
        EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )
    }

    pub fn get_next_account_nonce(&self, address: H160, state_root: H256) -> Result<U256> {
        let state_root_nonce = self.get_nonce_from_state_root(address, state_root)?;
        let mut nonce_store = self.nonce_store.lock().unwrap();
        match nonce_store.entry(address) {
            std::collections::hash_map::Entry::Vacant(_) => Ok(state_root_nonce),
            std::collections::hash_map::Entry::Occupied(e) => {
                let nonce_set = e.get();
                if !nonce_set.contains(&state_root_nonce) {
                    return Ok(state_root_nonce);
                }

                let mut nonce = state_root_nonce;
                for elem in nonce_set.range(state_root_nonce..) {
                    if (elem - nonce) > U256::from(1) {
                        break;
                    } else {
                        nonce = *elem;
                    }
                }
                nonce += U256::from(1);
                Ok(nonce)
            }
        }
    }

    pub fn store_account_nonce(&self, address: H160, nonce: U256) -> bool {
        let mut nonce_store = self.nonce_store.lock().unwrap();
        nonce_store.entry(address).or_insert_with(BTreeSet::new);

        match nonce_store.entry(address) {
            std::collections::hash_map::Entry::Occupied(mut e) => {
                e.get_mut().insert(nonce);
                true
            }
            _ => false,
        }
    }

    pub fn clear_account_nonce(&self) {
        let mut nonce_store = self.nonce_store.lock().unwrap();
        nonce_store.clear()
    }
}
