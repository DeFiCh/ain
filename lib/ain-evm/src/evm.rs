use std::{path::PathBuf, sync::Arc};

use ain_contracts::{
    get_dfi_instrinics_registry_contract, get_dfi_intrinsics_v1_contract, get_dst20_v1_contract,
    get_transfer_domain_contract, get_transfer_domain_v1_contract,
};
use anyhow::format_err;
use ethereum::{Block, PartialHeader};
use ethereum_types::{Bloom, H160, H256, H64, U256};
use log::{debug, trace};
use tokio::sync::{
    mpsc::{self, UnboundedReceiver, UnboundedSender},
    RwLock,
};

use crate::{
    backend::{EVMBackend, Vicinity},
    block::BlockService,
    blocktemplate::{
        BlockData, BlockTemplateData, ReceiptAndOptionalContractAddress, TemplateTxItem,
    },
    contract::{
        deploy_contract_tx, dfi_intrinsics_registry_deploy_info, dfi_intrinsics_v1_deploy_info,
        dst20_v1_deploy_info, get_dst20_migration_txs, reserve_dst20_namespace,
        reserve_intrinsics_namespace, transfer_domain_deploy_info,
        transfer_domain_v1_contract_deploy_info, DeployContractInfo,
    },
    core::{EVMCoreService, XHash},
    executor::{AinExecutor, ExecuteTx},
    filters::FilterService,
    log::{LogService, Notification},
    receipt::ReceiptService,
    storage::{traits::BlockStorage, Storage},
    transaction::SignedTx,
    trie::GENESIS_STATE_ROOT,
    Result,
};

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

pub struct ExecTxState {
    pub tx: Box<SignedTx>,
    pub receipt: ReceiptAndOptionalContractAddress,
    pub state_root: H256,
    pub logs_bloom: Bloom,
    pub gas_used: U256,
    pub gas_fees: U256,
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
    pub unsafe fn construct_block_in_template(
        &self,
        template_id: u64,
    ) -> Result<FinalizedBlockInfo> {
        let block_template = self.core.block_templates.get(template_id)?;
        let state_root = block_template.get_latest_state_root();
        let logs_bloom = block_template.get_latest_logs_bloom();
        let mut template = block_template.data.lock();

        let timestamp = template.timestamp;
        let parent_hash = template.parent_hash;
        let beneficiary = template.vicinity.beneficiary;
        let difficulty = template.vicinity.block_difficulty;
        let block_number = template.vicinity.block_number;
        let base_fee = template.vicinity.block_base_fee_per_gas;
        let gas_limit = template.vicinity.block_gas_limit;

        let txs_len = template.transactions.len();
        let mut all_transactions = Vec::with_capacity(txs_len);
        let mut receipts_v3: Vec<ReceiptAndOptionalContractAddress> = Vec::with_capacity(txs_len);
        let mut total_gas_fees = U256::zero();

        debug!("[construct_block] template_id: {:?}", template_id);
        debug!("[construct_block] vicinity: {:?}", template.vicinity);

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.core.trie_store),
            Arc::clone(&self.storage),
            template.vicinity.clone(),
        )?;

        let mut executor = AinExecutor::new(&mut backend);
        for template_tx in template.transactions.clone() {
            all_transactions.push(template_tx.tx);
            receipts_v3.push(template_tx.receipt_v3);
            total_gas_fees = total_gas_fees
                .checked_add(template_tx.gas_fees)
                .ok_or_else(|| format_err!("calculate total gas fees failed from overflow"))?;
        }

        let total_burnt_fees = template.total_gas_used * base_fee;
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

        let extra_data = format!("DFI: {}", template.dvm_block).into_bytes();
        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary,
                state_root: backend.commit(),
                receipts_root: ReceiptService::get_receipts_root(&receipts_v3),
                logs_bloom,
                difficulty,
                number: block_number,
                gas_limit,
                gas_used: template.total_gas_used,
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
            receipts_v3.clone(),
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

            self.channel
                .sender
                .send(Notification::Block(block.header.hash()))
                .map_err(|e| format_err!(e.to_string()))?;
        }
        self.core.block_templates.remove(template_id);
        self.core.clear_account_nonce();
        self.core.clear_transaction_cache();

        Ok(())
    }

    unsafe fn update_block_template_state_from_tx(
        &self,
        template_id: u64,
        tx: ExecuteTx,
    ) -> Result<ExecTxState> {
        let block_template = self.core.block_templates.get(template_id)?;
        let state_root = block_template.get_latest_state_root();
        let mut logs_bloom = block_template.get_latest_logs_bloom();
        debug!(
            "[update_block_template_state_from_tx] state_root : {:#?}",
            state_root
        );

        let template = block_template.data.lock();
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

        executor.update_total_gas_used(template.total_gas_used);
        let apply_tx = executor.execute_tx(tx, base_fee)?;
        EVMCoreService::logs_bloom(apply_tx.logs, &mut logs_bloom);

        Ok(ExecTxState {
            tx: apply_tx.tx,
            receipt: apply_tx.receipt,
            state_root: backend.commit(),
            logs_bloom,
            gas_used: apply_tx.used_gas,
            gas_fees: apply_tx.gas_fee,
        })
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn update_state_in_block_template(
        &self,
        template_id: u64,
        mnview_ptr: usize,
    ) -> Result<()> {
        // reserve DST20 namespace
        let block_template = self.core.block_templates.get(template_id)?;
        let is_evm_genesis_block = block_template.get_block_number() == U256::zero();
        let state_root = block_template.get_latest_state_root();
        let mut logs_bloom = block_template.get_latest_logs_bloom();

        let mut template = block_template.data.lock();
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

        if is_evm_genesis_block {
            reserve_dst20_namespace(&mut executor)?;
            reserve_intrinsics_namespace(&mut executor)?;

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
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                executor.commit(),
                logs_bloom,
            ));

            // Deploy DFIIntrinsics contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dfi_intrinsics_v1_deploy_info(template.dvm_block, template.vicinity.block_number)?;

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // DFIIntrinsics contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_dfi_intrinsics_v1_contract().contract.init_bytecode,
                &base_fee,
            )?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                executor.commit(),
                logs_bloom,
            ));

            // Deploy transfer domain contract on the first block
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = transfer_domain_v1_contract_deploy_info();

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // Transferdomain_v1 contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_transfer_domain_v1_contract().contract.init_bytecode,
                &base_fee,
            )?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                executor.commit(),
                logs_bloom,
            ));

            // Deploy transfer domain proxy
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = transfer_domain_deploy_info(get_transfer_domain_v1_contract().fixed_address)?;

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // Transferdomain contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_transfer_domain_contract().contract.init_bytecode,
                &base_fee,
            )?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                executor.commit(),
                logs_bloom,
            ));

            // Deploy DST20 implementation contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dst20_v1_deploy_info();
            trace!("deploying {:x?} bytecode {:?}", address, bytecode);

            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // DST20 implementation contract deployment TX
            let (tx, receipt) =
                deploy_contract_tx(get_dst20_v1_contract().contract.init_bytecode, &base_fee)?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                executor.commit(),
                logs_bloom,
            ));

            // Deploy DST20 migration TX
            let migration_txs = get_dst20_migration_txs(mnview_ptr)?;
            for exec_tx in migration_txs.clone() {
                let apply_result = executor.execute_tx(exec_tx, base_fee)?;
                EVMCoreService::logs_bloom(apply_result.logs, &mut logs_bloom);
                template.transactions.push(TemplateTxItem::new_system_tx(
                    apply_result.tx,
                    apply_result.receipt,
                    executor.commit(),
                    logs_bloom,
                ));
            }
        } else {
            let DeployContractInfo {
                address, storage, ..
            } = dfi_intrinsics_v1_deploy_info(template.dvm_block, template.vicinity.block_number)?;

            executor.update_storage(address, storage)?;
            executor.commit();
            template.initial_state_root = backend.commit();
        }
        Ok(())
    }
}

// Block template methods
impl EVMServices {
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn create_block_template(
        &self,
        dvm_block: u64,
        beneficiary: H160,
        difficulty: u32,
        timestamp: u64,
    ) -> Result<u64> {
        let (target_block, initial_state_root) = match self.storage.get_latest_block()? {
            None => (U256::zero(), GENESIS_STATE_ROOT), // Genesis block
            Some(block) => (block.header.number + 1, block.header.state_root),
        };

        let block_difficulty = U256::from(difficulty);
        let (parent_hash, _) = self
            .block
            .get_latest_block_hash_and_number()?
            .unwrap_or_default(); // Safe since calculate_base_fee will default to INITIAL_BASE_FEE
        let block_base_fee_per_gas = self.block.calculate_base_fee(parent_hash)?;

        let block_gas_limit = U256::from(self.storage.get_attributes_or_default()?.block_gas_limit);
        let template_id = self.core.block_templates.create(BlockTemplateData {
            transactions: Vec::new(),
            block_data: None,
            total_gas_used: U256::zero(),
            vicinity: Vicinity {
                beneficiary,
                block_number: target_block,
                timestamp: U256::from(timestamp),
                total_gas_used: U256::zero(),
                block_difficulty,
                block_gas_limit,
                block_base_fee_per_gas,
                block_randomness: None,
                ..Vicinity::default()
            },
            parent_hash,
            dvm_block,
            timestamp,
            initial_state_root,
        });
        Ok(template_id)
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
        tx: ExecuteTx,
        hash: XHash,
    ) -> Result<()> {
        let tx_update = self.update_block_template_state_from_tx(template_id, tx.clone())?;
        let tx_hash = tx_update.tx.hash();

        debug!(
            "[push_tx_in_block_template] Pushing new state_root {:x?} to template_id {}",
            tx_update.state_root, template_id
        );
        self.core
            .block_templates
            .push_in(template_id, tx_update, hash)?;
        self.filters.add_tx_to_filters(tx_hash);

        Ok(())
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
