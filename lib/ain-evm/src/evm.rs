use std::{path::PathBuf, sync::Arc};

use ain_contracts::{
    get_dfi_instrinics_registry_contract, get_dfi_intrinsics_v1_contract, get_dst20_v1_contract,
    get_transfer_domain_contract, get_transfer_domain_v1_contract, FixedContract,
};
use anyhow::format_err;
use ethereum::{Block, PartialHeader, ReceiptV3};
use ethereum_types::{Bloom, H160, H256, H64, U256};
use log::{debug, trace};

use crate::log::Notification;
use crate::{
    backend::{EVMBackend, Vicinity},
    block::BlockService,
    contract::{
        deploy_contract_tx, dfi_intrinsics_registry_deploy_info, dfi_intrinsics_v1_deploy_info,
        dst20_v1_deploy_info, get_dst20_migration_txs, reserve_dst20_namespace,
        reserve_intrinsics_namespace, transfer_domain_deploy_info,
        transfer_domain_v1_contract_deploy_info, DeployContractInfo,
    },
    core::{EVMCoreService, XHash},
    executor::AinExecutor,
    filters::FilterService,
    log::LogService,
    receipt::ReceiptService,
    storage::{traits::BlockStorage, Storage},
    transaction::{
        system::{DeployContractData, SystemTx},
        SignedTx,
    },
    trie::GENESIS_STATE_ROOT,
    txqueue::{BlockData, QueueTx},
    EVMError, Result,
};
use tokio::sync::mpsc::{self, UnboundedReceiver, UnboundedSender};
use tokio::sync::RwLock;

pub struct NotificationChannel<T> {
    pub sender: UnboundedSender<T>,
    pub receiver: RwLock<UnboundedReceiver<T>>,
}

pub struct EVMServices {
    pub core: EVMCoreService,
    pub block: BlockService,
    pub receipt: ReceiptService,
    pub logs: LogService,
    pub filters: FilterService,
    pub storage: Arc<Storage>,
    pub channel: NotificationChannel<Notification>,
}

pub struct FinalizedBlockInfo {
    pub block_hash: XHash,
    pub failed_transactions: Vec<XHash>,
    pub total_burnt_fees: U256,
    pub total_priority_fees: U256,
    pub block_number: U256,
}

pub type ReceiptAndOptionalContractAddress = (ReceiptV3, Option<H160>);

impl EVMServices {
    /// Constructs a new Handlers instance. Depending on whether the defid -ethstartstate flag is set,
    /// it either revives the storage from a previously saved state or initializes new storage using input from a JSON file.
    /// This JSON-based initialization is exclusively reserved for regtest environments.
    ///
    /// # Warning
    ///
    /// Loading state from JSON will overwrite previous stored state
    ///
    /// # Errors
    ///
    /// This method will return an error if an attempt is made to load a genesis state from a JSON file outside of a regtest environment.
    ///
    /// # Return
    ///
    /// Returns an instance of the struct, either restored from storage or created from a JSON file.
    pub fn new() -> Result<Self> {
        let (sender, receiver) = mpsc::unbounded_channel();
        let datadir = ain_cpp_imports::get_datadir();
        let path = PathBuf::from(datadir).join("evm");
        if !path.exists() {
            std::fs::create_dir(&path)?;
        }

        if let Some(state_input_path) = ain_cpp_imports::get_state_input_json() {
            if ain_cpp_imports::get_network() != "regtest" {
                return Err(format_err!(
                    "Loading a genesis from JSON file is restricted to regtest network"
                )
                .into());
            }
            let storage = Arc::new(Storage::new(&path)?);
            Ok(Self {
                core: EVMCoreService::new_from_json(
                    Arc::clone(&storage),
                    PathBuf::from(state_input_path),
                    path,
                )?,
                block: BlockService::new(Arc::clone(&storage))?,
                receipt: ReceiptService::new(Arc::clone(&storage)),
                logs: LogService::new(Arc::clone(&storage)),
                filters: FilterService::new(),
                storage,
                channel: NotificationChannel {
                    sender,
                    receiver: RwLock::new(receiver),
                },
            })
        } else {
            let storage = Arc::new(Storage::restore(&path)?);
            Ok(Self {
                core: EVMCoreService::restore(Arc::clone(&storage), path),
                block: BlockService::new(Arc::clone(&storage))?,
                receipt: ReceiptService::new(Arc::clone(&storage)),
                logs: LogService::new(Arc::clone(&storage)),
                filters: FilterService::new(),
                storage,
                channel: NotificationChannel {
                    sender,
                    receiver: RwLock::new(receiver),
                },
            })
        }
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn construct_block_in_queue(
        &self,
        queue_id: u64,
        difficulty: u32,
        beneficiary: H160,
        timestamp: u64,
        dvm_block_number: u64,
        mnview_ptr: usize,
    ) -> Result<FinalizedBlockInfo> {
        let tx_queue = self.core.tx_queues.get(queue_id)?;
        let mut queue = tx_queue.data.lock();

        let queue_txs_len = queue.transactions.len();
        let mut all_transactions = Vec::with_capacity(queue_txs_len);
        let mut failed_transactions = Vec::with_capacity(queue_txs_len);
        let mut receipts_v3: Vec<ReceiptAndOptionalContractAddress> =
            Vec::with_capacity(queue_txs_len);
        let mut total_gas_used = 0u64;
        let mut total_gas_fees = U256::zero();
        let mut logs_bloom: Bloom = Bloom::default();

        let parent_data = self.block.get_latest_block_hash_and_number()?;
        let state_root = self
            .storage
            .get_latest_block()?
            .map_or(GENESIS_STATE_ROOT, |block| block.header.state_root);

        debug!("[construct_block] queue_id: {:?}", queue_id);
        debug!("[construct_block] beneficiary: {:?}", beneficiary);
        let (vicinity, parent_hash, current_block_number) = match parent_data {
            None => (
                Vicinity {
                    beneficiary,
                    timestamp: U256::from(timestamp),
                    block_number: U256::zero(),
                    block_gas_limit: U256::from(
                        self.storage.get_attributes_or_default()?.block_gas_limit,
                    ),
                    ..Vicinity::default()
                },
                H256::zero(),
                U256::zero(),
            ),
            Some((hash, number)) => (
                Vicinity {
                    beneficiary,
                    timestamp: U256::from(timestamp),
                    block_number: number + 1,
                    block_gas_limit: U256::from(
                        self.storage.get_attributes_or_default()?.block_gas_limit,
                    ),
                    ..Vicinity::default()
                },
                hash,
                number + 1,
            ),
        };
        debug!("[construct_block] vincinity: {:?}", vicinity);

        let base_fee = self.block.calculate_base_fee(parent_hash)?;
        debug!("[construct_block] Block base fee: {}", base_fee);

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.core.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )?;

        let mut executor = AinExecutor::new(&mut backend);

        let is_evm_genesis_block = queue.target_block == U256::zero();
        if is_evm_genesis_block {
            // reserve DST20 namespace
            reserve_dst20_namespace(&mut executor)?;
            reserve_intrinsics_namespace(&mut executor)?;

            let migration_txs = get_dst20_migration_txs(mnview_ptr)?;
            queue.transactions.extend(migration_txs);

            // Deploy DFIIntrinsicsRegistry contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dfi_intrinsics_registry_deploy_info(get_dfi_intrinsics_v1_contract().fixed_address);

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // DFIIntrinsicsRegistry contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_dfi_instrinics_registry_contract()
                    .contract
                    .init_bytecode,
                &base_fee,
            )?;
            all_transactions.push(Box::new(tx));
            receipts_v3.push((receipt, Some(address)));

            // Deploy DFIIntrinsics contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dfi_intrinsics_v1_deploy_info(dvm_block_number, current_block_number)?;

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // DFIIntrinsics contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_dfi_intrinsics_v1_contract().contract.init_bytecode,
                &base_fee,
            )?;
            all_transactions.push(Box::new(tx));
            receipts_v3.push((receipt, Some(address)));

            // Deploy transfer domain contract on the first block
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = transfer_domain_v1_contract_deploy_info();

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();
            let (tx, receipt) = deploy_contract_tx(
                get_transfer_domain_v1_contract().contract.init_bytecode,
                &base_fee,
            )?;

            all_transactions.push(Box::new(tx));
            receipts_v3.push((receipt, Some(address)));

            // Deploy transfer domain proxy
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = transfer_domain_deploy_info(get_transfer_domain_v1_contract().fixed_address)?;
            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // transfer domain proxy deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_transfer_domain_contract().contract.init_bytecode,
                &base_fee,
            )?;
            all_transactions.push(Box::new(tx));
            receipts_v3.push((receipt, Some(address)));

            // deploy DST20 implementation contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dst20_v1_deploy_info();
            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            let (tx, receipt) =
                deploy_contract_tx(get_dst20_v1_contract().contract.init_bytecode, &base_fee)?;
            all_transactions.push(Box::new(tx));
            receipts_v3.push((receipt, Some(address)));
        } else {
            // Ensure that state root changes by updating counter contract storage
            let DeployContractInfo {
                address, storage, ..
            } = dfi_intrinsics_v1_deploy_info(dvm_block_number, current_block_number)?;
            executor.update_storage(address, storage)?;
            executor.commit();
        }

        debug!(
            "[construct_block] Processing {:?} transactions in queue",
            queue.transactions.len()
        );

        let mut exceed_block_limit = false;
        for queue_item in queue.transactions.clone() {
            executor.update_total_gas_used(U256::from(total_gas_used));
            let apply_result = match executor.apply_queue_tx(queue_item.tx, base_fee) {
                Ok(result) => result,
                Err(EVMError::BlockSizeLimit(message)) => {
                    debug!("[construct_block] {}", message);
                    failed_transactions.push(queue_item.tx_hash);
                    exceed_block_limit = true;
                    continue;
                }
                Err(e) => {
                    if exceed_block_limit {
                        failed_transactions.push(queue_item.tx_hash);
                        continue;
                    } else {
                        return Err(e);
                    }
                }
            };

            all_transactions.push(apply_result.tx);
            EVMCoreService::logs_bloom(apply_result.logs, &mut logs_bloom);
            receipts_v3.push(apply_result.receipt);
            total_gas_used += apply_result.used_gas;
            total_gas_fees += apply_result.gas_fee;

            executor.commit();
        }

        let total_burnt_fees = U256::from(total_gas_used) * base_fee;
        let total_priority_fees = total_gas_fees - total_burnt_fees;
        debug!(
            "[construct_block] Total burnt fees : {:#?}",
            total_burnt_fees
        );
        debug!(
            "[construct_block] Total priority fees : {:#?}",
            total_priority_fees
        );

        // burn base fee and pay priority fee to miner
        executor
            .backend
            .add_balance(beneficiary, total_priority_fees)?;
        executor.commit();

        let extra_data = format!("DFI: {dvm_block_number}").into_bytes();
        let gas_limit = self.storage.get_attributes_or_default()?.block_gas_limit;
        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary,
                state_root: backend.commit(),
                receipts_root: ReceiptService::get_receipts_root(&receipts_v3),
                logs_bloom,
                difficulty: U256::from(difficulty),
                number: current_block_number,
                gas_limit: U256::from(gas_limit),
                gas_used: U256::from(total_gas_used),
                timestamp,
                extra_data,
                mix_hash: H256::default(),
                nonce: H64::default(),
                base_fee,
            },
            all_transactions
                .iter()
                .map(|signed_tx| signed_tx.transaction.clone())
                .collect(),
            Vec::new(),
        );
        let block_hash = format!("{:?}", block.header.hash());
        let receipts = self.receipt.generate_receipts(
            &all_transactions,
            receipts_v3,
            block.header.hash(),
            block.header.number,
            base_fee,
        );
        queue.block_data = Some(BlockData { block, receipts });

        Ok(FinalizedBlockInfo {
            block_hash,
            failed_transactions,
            total_burnt_fees,
            total_priority_fees,
            block_number: current_block_number,
        })
    }

    pub fn get_block_limit(&self) -> Result<u64> {
        let res = self.storage.get_attributes_or_default()?;
        Ok(res.block_gas_limit)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn commit_queue(&self, queue_id: u64) -> Result<()> {
        {
            let tx_queue = self.core.tx_queues.get(queue_id)?;
            let queue = tx_queue.data.lock();
            let Some(BlockData { block, receipts }) = queue.block_data.clone() else {
                return Err(format_err!("no constructed EVM block exist in queue id").into());
            };

            debug!(
                "[finalize_block] Finalizing block number {:#x}, state_root {:#x}",
                block.header.number, block.header.state_root
            );

            self.block.connect_block(&block)?;
            self.logs
                .generate_logs_from_receipts(&receipts, block.header.number)?;
            self.receipt.put_receipts(receipts)?;
            self.filters.add_block_to_filters(block.header.hash());

            self.channel
                .sender
                .send(Notification::Block(block.header.hash()))
                .map_err(|e| format_err!(e.to_string()))?;
        }
        self.core.tx_queues.remove(queue_id);
        self.core.clear_account_nonce();
        self.core.clear_transaction_cache();

        Ok(())
    }

    unsafe fn update_queue_state_from_tx(
        &self,
        queue_id: u64,
        tx: QueueTx,
    ) -> Result<(SignedTx, U256, H256)> {
        let (target_block, state_root, timestamp, is_first_tx) = {
            let queue = self.core.tx_queues.get(queue_id)?;

            let state_root = queue.get_latest_state_root();
            let data = queue.data.lock();

            (
                data.target_block,
                state_root,
                data.timestamp,
                data.transactions.is_empty(),
            )
        };
        debug!(
            "[update_queue_state_from_tx] state_root : {:#?}",
            state_root
        );

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.core.trie_store),
            Arc::clone(&self.storage),
            Vicinity {
                timestamp: U256::from(timestamp),
                block_number: target_block,
                block_gas_limit: U256::from(
                    self.storage.get_attributes_or_default()?.block_gas_limit,
                ),
                ..Vicinity::default()
            },
        )?;

        let mut executor = AinExecutor::new(&mut backend);

        let (parent_hash, _) = self
            .block
            .get_latest_block_hash_and_number()?
            .unwrap_or_default(); // Safe since calculate_base_fee will default to INITIAL_BASE_FEE
        let base_fee = self.block.calculate_base_fee(parent_hash)?;

        // Update instrinsics contract to reproduce construct_block behaviour
        if is_first_tx {
            let (current_native_height, _) = ain_cpp_imports::get_sync_status().unwrap();
            let DeployContractInfo {
                address, storage, ..
            } = dfi_intrinsics_v1_deploy_info((current_native_height + 1) as u64, target_block)?;
            executor.update_storage(address, storage)?;
            executor.commit();
        }

        let apply_tx = executor.apply_queue_tx(tx, base_fee)?;

        Ok((
            *apply_tx.tx,
            U256::from(apply_tx.used_gas),
            backend.commit(),
        ))
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn push_tx_in_queue(&self, queue_id: u64, tx: QueueTx, hash: XHash) -> Result<()> {
        let (signed_tx, gas_used, state_root) =
            self.update_queue_state_from_tx(queue_id, tx.clone())?;

        debug!(
            "[push_tx_in_queue] Pushing new state_root {:x?} to queue_id {}",
            state_root, queue_id
        );
        self.core
            .tx_queues
            .push_in(queue_id, tx, hash, gas_used, state_root)?;

        self.filters.add_tx_to_filters(signed_tx.transaction.hash());

        self.channel
            .sender
            .send(Notification::Transaction(signed_tx.transaction.hash()))
            .map_err(|e| format_err!(e.to_string()))?;

        Ok(())
    }

    pub fn verify_tx_fees(&self, tx: &str) -> Result<U256> {
        trace!("[verify_tx_fees] raw transaction : {:#?}", tx);
        let signed_tx = self
            .core
            .signed_tx_cache
            .try_get_or_create(tx)
            .map_err(|_| format_err!("Error: decoding raw tx to TransactionV2"))?;
        trace!("[verify_tx_fees] signed_tx : {:#?}", signed_tx);

        let block_fee = self.block.calculate_next_block_base_fee()?;
        let tx_gas_price = signed_tx.gas_price();
        if tx_gas_price < block_fee {
            trace!("[verify_tx_fees] tx gas price is lower than block base fee");
            return Err(format_err!("tx gas price is lower than block base fee").into());
        }

        Ok(block_fee)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn is_dst20_deployed_or_queued(
        &self,
        queue_id: u64,
        name: &str,
        symbol: &str,
        token_id: u64,
    ) -> Result<bool> {
        let address = ain_contracts::dst20_address_from_token_id(token_id)?;
        debug!(
            "[is_dst20_deployed_or_queued] Fetching address {:#?}",
            address
        );

        let backend = self.core.get_latest_block_backend()?;
        // Address already deployed
        match backend.get_account(&address) {
            None => {}
            Some(account) => {
                let FixedContract { contract, .. } = get_transfer_domain_contract();
                if account.code_hash == contract.codehash {
                    return Ok(true);
                }
            }
        }

        let deploy_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
            name: String::from(name),
            symbol: String::from(symbol),
            token_id,
            address,
        }));

        let is_queued = self.core.tx_queues.get(queue_id)?.is_queued(&deploy_tx);

        Ok(is_queued)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn is_smart_contract_in_queue(&self, address: H160, queue_id: u64) -> Result<bool> {
        let backend = self
            .core
            .get_backend(self.core.tx_queues.get_latest_state_root_in(queue_id)?)?;

        Ok(match backend.get_account(&address) {
            None => false,
            Some(account) => account.code_hash != H256::zero(),
        })
    }

    pub fn get_nonce(&self, address: H160, state_root: H256) -> Result<U256> {
        let backend = self.core.get_backend(state_root)?;
        let nonce = backend.get_nonce(&address);
        Ok(nonce)
    }
}
