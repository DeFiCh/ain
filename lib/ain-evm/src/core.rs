use std::{path::PathBuf, sync::Arc};

use anyhow::format_err;
use ethereum::{AccessList, Account, Block, Log, PartialHeader, TransactionV2};
use ethereum_types::{Bloom, BloomInput, H160, H256, U256};
use log::debug;
use vsdb_core::vsdb_set_base_dir;

use crate::{
    backend::{BackendError, EVMBackend, InsufficientBalance, Vicinity},
    block::INITIAL_BASE_FEE,
    executor::{AinExecutor, TxResponse},
    fee::calculate_prepay_gas_fee,
    gas::check_tx_intrinsic_gas,
    receipt::ReceiptService,
    storage::{traits::BlockStorage, Storage},
    traits::{Executor, ExecutorContext},
    transaction::{
        system::{SystemTx, TransferDirection, TransferDomainData},
        SignedTx,
    },
    trie::TrieDBStore,
    txqueue::{QueueTx, TransactionQueueMap},
    weiamount::WeiAmount,
    Result,
};

pub type XHash = String;

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
    pub gas_price: Option<U256>,
    pub max_fee_per_gas: Option<U256>,
    pub access_list: AccessList,
    pub block_number: U256,
    pub transaction_type: Option<U256>,
}

pub struct ValidateTxInfo {
    pub signed_tx: SignedTx,
    pub prepay_fee: U256,
    pub used_gas: u64,
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
    /// The validation checks of the tx before we consider it to be valid are:
    /// 1. Account nonce check: verify that the tx nonce must be more than or equal to the account nonce.
    /// 2. Gas price check: verify that the maximum gas price is minimally of the block initial base fee.
    /// 3. Gas price and tx value check: verify that amount is within money range.
    /// 4. Account balance check: verify that the account balance must minimally have the tx prepay gas fee.
    /// 5. Intrinsic gas limit check: verify that the tx intrinsic gas is within the tx gas limit.
    /// 6. Gas limit check: verify that the tx gas limit is not higher than the maximum gas per block.
    ///
    /// # Arguments
    ///
    /// * `tx` - The raw tx.
    /// * `queue_id` - The queue_id queue number.
    /// * `use_context` - Flag to call tx with stack executor.
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
    pub unsafe fn validate_raw_tx(&self, tx: &str, queue_id: u64) -> Result<ValidateTxInfo> {
        debug!("[validate_raw_tx] raw transaction : {:#?}", tx);
        let signed_tx = SignedTx::try_from(tx)
            .map_err(|_| format_err!("Error: decoding raw tx to TransactionV2"))?;
        debug!(
            "[validate_raw_tx] TransactionV2 : {:#?}",
            signed_tx.transaction
        );

        let block_number = self
            .storage
            .get_latest_block()?
            .map(|block| block.header.number)
            .unwrap_or_default();
        debug!("[validate_raw_tx] block_number : {:#?}", block_number);

        let signed_tx: SignedTx = tx.try_into()?;
        let nonce = self
            .get_nonce(signed_tx.sender, block_number)
            .map_err(|e| format_err!("Error getting nonce {e}"))?;
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
            return Err(format_err!(
                "Invalid nonce. Account nonce {}, signed_tx nonce {}",
                nonce,
                signed_tx.nonce()
            )
            .into());
        }

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

        let balance = self
            .get_balance(signed_tx.sender, block_number)
            .map_err(|e| format_err!("Error getting balance {e}"))?;
        let prepay_fee = calculate_prepay_gas_fee(&signed_tx)?;
        debug!("[validate_raw_tx] Account balance : {:x?}", balance);
        debug!("[validate_raw_tx] prepay_fee : {:x?}", prepay_fee);

        // Validate tx prepay fees with account balance
        if balance < prepay_fee {
            debug!("[validate_raw_tx] insufficient balance to pay fees");
            return Err(format_err!("insufficient balance to pay fees").into());
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

        let use_queue = queue_id != 0;
        let used_gas = if use_queue {
            let TxResponse { used_gas, .. } = self.call(EthCallArgs {
                caller: Some(signed_tx.sender),
                to: signed_tx.to(),
                value: signed_tx.value(),
                data: signed_tx.data(),
                gas_limit: signed_tx.gas_limit().as_u64(),
                access_list: signed_tx.access_list(),
                block_number,
                gas_price: Some(tx_gas_price),
                max_fee_per_gas: signed_tx.max_fee_per_gas(),
                transaction_type: Some(signed_tx.get_tx_type()),
            })?;
            used_gas
        } else {
            u64::default()
        };

        // Validate total gas usage in queued txs exceeds block size
        if use_queue {
            debug!("[validate_raw_tx] used_gas: {:#?}", used_gas);
            let total_current_gas_used = self
                .tx_queues
                .get_total_gas_used_in(queue_id)
                .unwrap_or_default();

            let block_gas_limit = self.storage.get_attributes_or_default()?.block_gas_limit;
            if total_current_gas_used + U256::from(used_gas) > U256::from(block_gas_limit) {
                return Err(format_err!("Tx can't make it in block. Block size limit {}, pending block gas used : {:x?}, tx used gas : {:x?}, total : {:x?}", block_gas_limit, total_current_gas_used, U256::from(used_gas), total_current_gas_used + U256::from(used_gas)).into());
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
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn add_balance(
        &self,
        queue_id: u64,
        signed_tx: SignedTx,
        hash: XHash,
    ) -> Result<()> {
        let queue_tx = QueueTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
            signed_tx: Box::new(signed_tx),
            direction: TransferDirection::EvmIn,
        }));
        self.tx_queues
            .push_in(queue_id, queue_tx, hash, U256::zero())?;
        Ok(())
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn sub_balance(
        &self,
        queue_id: u64,
        signed_tx: SignedTx,
        hash: XHash,
    ) -> Result<()> {
        let block_number = self
            .storage
            .get_latest_block()?
            .map_or(U256::default(), |block| block.header.number);
        let balance = self.get_balance(signed_tx.sender, block_number)?;
        if balance < signed_tx.value() {
            Err(BackendError::InsufficientBalance(InsufficientBalance {
                address: signed_tx.sender,
                account_balance: balance,
                amount: signed_tx.value(),
            })
            .into())
        } else {
            let queue_tx = QueueTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
                signed_tx: Box::new(signed_tx),
                direction: TransferDirection::EvmOut,
            }));
            self.tx_queues
                .push_in(queue_id, queue_tx, hash, U256::zero())?;
            Ok(())
        }
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn create_queue(&self) -> Result<u64> {
        let target_block = match self.storage.get_latest_block()? {
            None => U256::zero(), // Genesis queue
            Some(block) => block.header.number + 1,
        };
        let queue_id = self.tx_queues.create(target_block);
        Ok(queue_id)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_queue(&self, queue_id: u64) {
        self.tx_queues.remove(queue_id);
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_txs_by_sender_in(&self, queue_id: u64, address: H160) -> Result<()> {
        self.tx_queues.remove_by_sender_in(queue_id, address)?;
        Ok(())
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_target_block_in(&self, queue_id: u64) -> Result<U256> {
        let target_block = self.tx_queues.get_target_block_in(queue_id)?;
        Ok(target_block)
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
        let nonce = match self.tx_queues.get_next_valid_nonce_in(queue_id, address)? {
            Some(nonce) => Ok(nonce),
            None => {
                let block_number = self
                    .storage
                    .get_latest_block()?
                    .map_or_else(U256::zero, |block| block.header.number);

                self.get_nonce(address, block_number)
            }
        }?;

        debug!(
            "Account {:x?} nonce {:x?} in queue_id {queue_id}",
            address, nonce
        );
        Ok(nonce)
    }
}

// State methods
impl EVMCoreService {
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
        let state_root = self
            .storage
            .get_latest_block()?
            .map_or(H256::default(), |block| block.header.state_root);

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

    pub fn get_nonce(&self, address: H160, block_number: U256) -> Result<U256> {
        let nonce = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.nonce);

        debug!("Account {:x?} nonce {:x?}", address, nonce);
        Ok(nonce)
    }

    pub fn get_latest_block_backend(&self) -> Result<EVMBackend> {
        let (state_root, block_number) = self
            .storage
            .get_latest_block()?
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
            Vicinity::default(),
        )
    }
}
