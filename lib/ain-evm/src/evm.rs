use std::{path::PathBuf, sync::Arc};

use ain_contracts::{
    get_dfi_instrinics_registry_contract, get_dfi_intrinsics_v1_contract, get_dst20_v1_contract,
    get_transfer_domain_contract, get_transfer_domain_v1_contract, FixedContract,
};
use anyhow::format_err;
use ethereum::{Block, PartialHeader};
use ethereum_types::{H160, H256, H64, U256};
use log::{debug, trace};

use crate::{
    backend::EVMBackend,
    block::BlockService,
    blocktemplate::{BlockData, QueueTx},
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
    storage::Storage,
    transaction::{
        system::{DeployContractData, SystemTx},
        SignedTx,
    },
    Result,
};

pub struct EVMServices {
    pub core: EVMCoreService,
    pub block: BlockService,
    pub receipt: ReceiptService,
    pub logs: LogService,
    pub filters: FilterService,
    pub storage: Arc<Storage>,
}

pub struct FinalizedBlockInfo {
    pub block_hash: XHash,
    pub total_burnt_fees: U256,
    pub total_priority_fees: U256,
    pub block_number: U256,
}

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
            })
        }
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn construct_block_in_template(
        &self,
        template_id: u64,
        difficulty: u32,
    ) -> Result<FinalizedBlockInfo> {
        let block_template = self.core.block_templates.get(template_id)?;
        let state_root = block_template.get_latest_state_root();
        let mut template = block_template.data.lock();

        let timestamp = template.timestamp;
        let beneficiary = template.vicinity.beneficiary;
        let block_number = template.vicinity.block_number;
        let total_gas_used = template.vicinity.total_gas_used;

        debug!("[construct_block] template_id: {:?}", template_id);
        debug!("[construct_block] vicinity: {:?}", template.vicinity);

        let (parent_hash, _) = self
            .block
            .get_latest_block_hash_and_number()?
            .unwrap_or_default(); // Safe since calculate_base_fee will default to INITIAL_BASE_FEE
        let base_fee = self.block.calculate_base_fee(parent_hash)?;
        debug!("[construct_block] Block base fee: {}", base_fee);

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.core.trie_store),
            Arc::clone(&self.storage),
            template.vicinity.clone(),
        )?;

        let mut executor = AinExecutor::new(&mut backend);
        let total_burnt_fees = total_gas_used * base_fee;
        let total_priority_fees = template.total_gas_fees - total_burnt_fees;
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

        let extra_data = format!("DFI: {}", template.dvm_block).into_bytes();
        let gas_limit = self.storage.get_attributes_or_default()?.block_gas_limit;
        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary,
                state_root: backend.commit(),
                receipts_root: ReceiptService::get_receipts_root(&template.receipts_v3),
                logs_bloom: template.logs_bloom,
                difficulty: U256::from(difficulty),
                number: block_number,
                gas_limit: U256::from(gas_limit),
                gas_used: total_gas_used,
                timestamp,
                extra_data,
                mix_hash: H256::default(),
                nonce: H64::default(),
                base_fee,
            },
            template
                .all_transactions
                .iter()
                .map(|signed_tx| signed_tx.transaction.clone())
                .collect(),
            Vec::new(),
        );
        let block_hash = format!("{:?}", block.header.hash());
        let receipts = self.receipt.generate_receipts(
            &template.all_transactions,
            template.receipts_v3.clone(),
            block.header.hash(),
            block.header.number,
            base_fee,
        );
        template.block_data = Some(BlockData { block, receipts });

        Ok(FinalizedBlockInfo {
            block_hash,
            total_burnt_fees,
            total_priority_fees,
            block_number,
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
    pub unsafe fn commit_block(&self, template_id: u64) -> Result<()> {
        {
            let block_template = self.core.block_templates.get(template_id)?;
            let template = block_template.data.lock();
            let Some(BlockData { block, receipts }) = template.block_data.clone() else {
                return Err(format_err!("no constructed EVM block exist in template id").into());
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
        }
        self.core.block_templates.remove(template_id);
        self.core.clear_account_nonce();
        self.core.clear_transaction_cache();

        Ok(())
    }

    unsafe fn update_block_template_state_from_tx(
        &self,
        template_id: u64,
        tx: QueueTx,
        mnview_ptr: usize,
    ) -> Result<(SignedTx, U256, H256)> {
        let block_template = self.core.block_templates.get(template_id)?;
        let state_root = block_template.get_latest_state_root();
        let mut template = block_template.data.lock();

        let is_first_tx = template.transactions_queue.is_empty();
        debug!(
            "[update_block_template_state_from_tx] state_root : {:#?}",
            state_root
        );

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.core.trie_store),
            Arc::clone(&self.storage),
            template.vicinity.clone(),
        )?;

        let mut executor = AinExecutor::new(&mut backend);

        let (parent_hash, _) = self
            .block
            .get_latest_block_hash_and_number()?
            .unwrap_or_default(); // Safe since calculate_base_fee will default to INITIAL_BASE_FEE
        let base_fee = self.block.calculate_base_fee(parent_hash)?;
        debug!(
            "[update_block_template_state_from_tx] Block base fee: {}",
            base_fee
        );

        // Update instrinsics contract to reproduce construct_block behaviour
        if is_first_tx {
            let is_evm_genesis_block = template.vicinity.block_number == U256::zero();
            if is_evm_genesis_block {
                // reserve DST20 namespace
                reserve_dst20_namespace(&mut executor)?;
                reserve_intrinsics_namespace(&mut executor)?;

                let migration_txs = get_dst20_migration_txs(mnview_ptr)?;
                template.transactions_queue.extend(migration_txs);

                // Deploy DFIIntrinsicsRegistry contract
                let DeployContractInfo {
                    address,
                    storage,
                    bytecode,
                } = dfi_intrinsics_registry_deploy_info(
                    get_dfi_intrinsics_v1_contract().fixed_address,
                );

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
                template.all_transactions.push(Box::new(tx));
                template.receipts_v3.push((receipt, Some(address)));

                // Deploy DFIIntrinsics contract
                let DeployContractInfo {
                    address,
                    storage,
                    bytecode,
                } = dfi_intrinsics_v1_deploy_info(
                    template.dvm_block,
                    template.vicinity.block_number,
                )?;

                trace!("deploying {:x?} bytecode {:?}", address, bytecode);
                executor.deploy_contract(address, bytecode, storage)?;
                executor.commit();

                // DFIIntrinsics contract deployment TX
                let (tx, receipt) = deploy_contract_tx(
                    get_dfi_intrinsics_v1_contract().contract.init_bytecode,
                    &base_fee,
                )?;
                template.all_transactions.push(Box::new(tx));
                template.receipts_v3.push((receipt, Some(address)));

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

                template.all_transactions.push(Box::new(tx));
                template.receipts_v3.push((receipt, Some(address)));

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
                template.all_transactions.push(Box::new(tx));
                template.receipts_v3.push((receipt, Some(address)));

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
                template.all_transactions.push(Box::new(tx));
                template.receipts_v3.push((receipt, Some(address)));
            } else {
                let DeployContractInfo {
                    address, storage, ..
                } = dfi_intrinsics_v1_deploy_info(
                    template.dvm_block,
                    template.vicinity.block_number,
                )?;
                executor.update_storage(address, storage)?;
                executor.commit();
            }
        }

        executor.update_total_gas_used(template.total_gas_used);
        let apply_tx = executor.apply_queue_tx(tx, base_fee)?;
        template.all_transactions.push(apply_tx.tx.clone());
        EVMCoreService::logs_bloom(apply_tx.logs, &mut template.logs_bloom);
        template.receipts_v3.push(apply_tx.receipt);
        template.total_gas_used = U256::from(apply_tx.used_gas);
        template.total_gas_fees = apply_tx.gas_fee;

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
    pub unsafe fn push_tx_in_block_template(
        &self,
        template_id: u64,
        tx: QueueTx,
        hash: XHash,
        mnptr: usize,
    ) -> Result<()> {
        let (signed_tx, gas_used, state_root) =
            self.update_block_template_state_from_tx(template_id, tx.clone(), mnptr)?;

        debug!(
            "[push_tx_in_block_template] Pushing new state_root {:x?} to template_id {}",
            state_root, template_id
        );
        self.core
            .block_templates
            .push_in(template_id, tx, hash, gas_used, state_root)?;

        self.filters.add_tx_to_filters(signed_tx.transaction.hash());

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
        template_id: u64,
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

        let is_queued = self
            .core
            .block_templates
            .get(template_id)?
            .is_queued(&deploy_tx);

        Ok(is_queued)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn is_smart_contract_in_block_template(
        &self,
        address: H160,
        template_id: u64,
    ) -> Result<bool> {
        let backend = self.core.get_backend(
            self.core
                .block_templates
                .get_latest_state_root_in(template_id)?,
        )?;

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
