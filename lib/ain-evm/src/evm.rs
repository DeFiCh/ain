use std::{path::PathBuf, sync::Arc};

use ain_contracts::{
    get_dfi_instrinics_registry_contract, get_dfi_intrinsics_v1_contract, get_dst20_v1_contract,
    get_dst20_v2_contract, get_transfer_domain_contract, get_transfer_domain_v1_contract,
    IMPLEMENTATION_SLOT,
};
use ain_cpp_imports::{get_df23_height, Attributes};
use anyhow::format_err;
use ethereum::{Block, PartialHeader};
use ethereum_types::{Bloom, H160, H256, H64, U256};
use log::{debug, info, trace};
use vsdb_core::vsdb_set_base_dir;

use crate::{
    backend::{EVMBackend, Vicinity},
    block::BlockService,
    blocktemplate::{BlockData, BlockTemplate, ReceiptAndOptionalContractAddress, TemplateTxItem},
    contract::{
        deploy_contract_tx, dfi_intrinsics_registry_deploy_info, dfi_intrinsics_v1_deploy_info,
        dst20::{
            dst20_v1_deploy_info, dst20_v2_deploy_info, get_dst20_migration_txs,
            reserve_dst20_namespace,
        },
        h160_to_h256, reserve_intrinsics_namespace, transfer_domain_deploy_info,
        transfer_domain_v1_contract_deploy_info, DeployContractInfo,
    },
    core::{EVMCoreService, XHash},
    executor::AinExecutor,
    filters::FilterService,
    log::LogService,
    receipt::ReceiptService,
    storage::{
        traits::{BlockStorage, FlushableStorage},
        Storage,
    },
    subscription::{Notification, SubscriptionService},
    trace::service::TracerService,
    transaction::{cache::TransactionCache, system::ExecuteTx, SignedTx},
    trie::{TrieDBStore, GENESIS_STATE_ROOT},
    Result,
};

pub struct EVMServices {
    pub core: EVMCoreService,
    pub block: BlockService,
    pub receipt: ReceiptService,
    pub logs: LogService,
    pub filters: FilterService,
    pub subscriptions: SubscriptionService,
    pub tracer: TracerService,
    pub storage: Arc<Storage>,
    pub tx_cache: Arc<TransactionCache>,
}

pub struct ExecTxState {
    pub tx: Box<SignedTx>,
    pub receipt: ReceiptAndOptionalContractAddress,
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

pub struct BlockContext {
    parent_hash: H256,
    pub dvm_block: u64,
    pub mnview_ptr: usize,
    pub attrs: Attributes,
}

fn init_vsdb(path: PathBuf) {
    info!(target: "vsdb", "Initializating VSDB");
    let vsdb_dir_path = path.join(".vsdb");
    vsdb_set_base_dir(&vsdb_dir_path).expect("Could not update vsdb base dir");
    info!(target: "vsdb", "VSDB directory : {}", vsdb_dir_path.display());
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
        init_vsdb(path.clone());

        if let Some(state_input_path) = ain_cpp_imports::get_state_input_json() {
            if ain_cpp_imports::get_network() != "regtest" {
                return Err(format_err!(
                    "Loading a genesis from JSON file is restricted to regtest network"
                )
                .into());
            }

            // Init storage
            let trie_store = Arc::new(TrieDBStore::new());
            let storage = Arc::new(Storage::new(&path)?);
            let tx_cache = Arc::new(TransactionCache::new());

            Ok(Self {
                core: EVMCoreService::new_from_json(
                    Arc::clone(&trie_store),
                    Arc::clone(&storage),
                    Arc::clone(&tx_cache),
                    PathBuf::from(state_input_path),
                )?,
                block: BlockService::new(Arc::clone(&storage))?,
                receipt: ReceiptService::new(Arc::clone(&storage)),
                logs: LogService::new(Arc::clone(&storage)),
                filters: FilterService::new(Arc::clone(&storage), Arc::clone(&tx_cache)),
                subscriptions: SubscriptionService::new(),
                tracer: TracerService::new(Arc::clone(&trie_store), Arc::clone(&storage)),
                storage,
                tx_cache,
            })
        } else {
            // Init storage
            let trie_store = Arc::new(TrieDBStore::restore());
            let storage = Arc::new(Storage::restore(&path)?);
            let tx_cache = Arc::new(TransactionCache::new());

            Ok(Self {
                core: EVMCoreService::restore(
                    Arc::clone(&trie_store),
                    Arc::clone(&storage),
                    Arc::clone(&tx_cache),
                ),
                block: BlockService::new(Arc::clone(&storage))?,
                receipt: ReceiptService::new(Arc::clone(&storage)),
                logs: LogService::new(Arc::clone(&storage)),
                filters: FilterService::new(Arc::clone(&storage), Arc::clone(&tx_cache)),
                subscriptions: SubscriptionService::new(),
                tracer: TracerService::new(Arc::clone(&trie_store), Arc::clone(&storage)),
                storage,
                tx_cache,
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
        template: &mut BlockTemplate,
        is_miner: bool,
    ) -> Result<FinalizedBlockInfo> {
        let logs_bloom = template.get_latest_logs_bloom();

        let parent_hash = template.ctx.parent_hash;
        let timestamp = template.vicinity.timestamp;
        let beneficiary = template.vicinity.beneficiary;
        let difficulty = template.vicinity.block_difficulty;
        let block_number = template.vicinity.block_number;
        let base_fee = template.vicinity.block_base_fee_per_gas;
        let gas_limit = template.vicinity.block_gas_limit;

        let txs_len = template.transactions.len();
        let mut all_transactions = Vec::with_capacity(txs_len);
        let mut receipts_v3: Vec<ReceiptAndOptionalContractAddress> = Vec::with_capacity(txs_len);
        let mut total_gas_fees = U256::zero();

        trace!("[construct_block] vicinity: {:?}", template.vicinity);

        let mut executor = AinExecutor::new(&mut template.backend);
        for template_tx in template.transactions.clone() {
            all_transactions.push(template_tx.tx);
            receipts_v3.push(template_tx.receipt_v3);
            total_gas_fees = total_gas_fees
                .checked_add(template_tx.gas_fees)
                .ok_or_else(|| format_err!("calculate total gas fees failed from overflow"))?;
        }

        let total_burnt_fees = template
            .total_gas_used
            .checked_mul(base_fee)
            .ok_or_else(|| format_err!("total_burnt_fees overflow"))?;
        let total_priority_fees = total_gas_fees
            .checked_sub(total_burnt_fees)
            .ok_or_else(|| format_err!("total_priority_fees underflow"))?;
        trace!(
            "[construct_block] Total burnt fees : {:#?}",
            total_burnt_fees
        );
        trace!(
            "[construct_block] Total priority fees : {:#?}",
            total_priority_fees
        );

        // burn base fee and pay priority fee to miner
        executor
            .backend
            .add_balance(beneficiary, total_priority_fees)?;

        let state_root = executor.commit(is_miner)?;

        let extra_data = format!("DFI: {}", template.ctx.dvm_block).into_bytes();
        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary,
                state_root,
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
        let block_hash = block.header.hash().to_fixed_bytes();
        let receipts = self.receipt.generate_receipts(
            &all_transactions,
            receipts_v3.clone(),
            block.header.hash(),
            block.header.number,
            base_fee,
        )?;
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
    pub unsafe fn commit_block(&self, template: &BlockTemplate) -> Result<()> {
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
        self.subscriptions
            .send(Notification::Block(block.header.hash()))?;
        self.core.clear_account_nonce();
        Ok(())
    }

    unsafe fn update_block_template_state_from_tx(
        &self,
        template: &mut BlockTemplate,
        tx: ExecuteTx,
    ) -> Result<ExecTxState> {
        let base_fee = template.get_block_base_fee_per_gas();
        let mut logs_bloom = template.get_latest_logs_bloom();

        let mut executor = AinExecutor::new(&mut template.backend);

        executor.update_total_gas_used(template.total_gas_used);
        match executor.execute_tx(tx, base_fee, Some(&template.ctx)) {
            Ok(apply_tx) => {
                EVMCoreService::logs_bloom(apply_tx.logs, &mut logs_bloom);
                template.backend.increase_tx_count();

                Ok(ExecTxState {
                    tx: apply_tx.tx,
                    receipt: apply_tx.receipt,
                    logs_bloom,
                    gas_used: apply_tx.used_gas,
                    gas_fees: apply_tx.gas_fee,
                })
            }
            Err(e) => {
                template.backend.reset_to_last_changeset();
                Err(e)
            }
        }
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn update_state_in_block_template(
        &self,
        template: &mut BlockTemplate,
    ) -> Result<()> {
        // reserve DST20 namespace;
        let is_evm_genesis_block = template.get_block_number() == U256::zero();
        let is_df23_fork = template.ctx.dvm_block == get_df23_height();
        let mut logs_bloom = template.get_latest_logs_bloom();

        let mut executor = AinExecutor::new(&mut template.backend);
        let base_fee = template.vicinity.block_base_fee_per_gas;
        trace!(
            "[update_state_in_block_template] Block base fee: {}",
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
            } = dfi_intrinsics_registry_deploy_info(vec![
                get_dfi_intrinsics_v1_contract().fixed_address,
            ]);

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;

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
                logs_bloom,
            ));

            // Deploy DFIIntrinsics contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dfi_intrinsics_v1_deploy_info(
                template.ctx.dvm_block,
                template.vicinity.block_number,
            )?;

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;

            // DFIIntrinsics contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_dfi_intrinsics_v1_contract().contract.init_bytecode,
                &base_fee,
            )?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
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

            // Transferdomain_v1 contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_transfer_domain_v1_contract().contract.init_bytecode,
                &base_fee,
            )?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
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

            // Transferdomain contract deployment TX
            let (tx, receipt) = deploy_contract_tx(
                get_transfer_domain_contract().contract.init_bytecode,
                &base_fee,
            )?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
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

            // DST20 implementation contract deployment TX
            let (tx, receipt) =
                deploy_contract_tx(get_dst20_v1_contract().contract.init_bytecode, &base_fee)?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                logs_bloom,
            ));

            // Deploy DST20 migration TX
            let migration_txs = get_dst20_migration_txs(template.ctx.mnview_ptr)?;
            for exec_tx in migration_txs.clone() {
                let apply_result = executor.execute_tx(exec_tx, base_fee, Some(&template.ctx))?;
                EVMCoreService::logs_bloom(apply_result.logs, &mut logs_bloom);
                template.transactions.push(TemplateTxItem::new_system_tx(
                    apply_result.tx,
                    apply_result.receipt,
                    logs_bloom,
                ));
            }
        } else {
            let DeployContractInfo {
                address, storage, ..
            } = dfi_intrinsics_v1_deploy_info(
                template.ctx.dvm_block,
                template.vicinity.block_number,
            )?;

            executor.update_storage(address, storage)?;
        }

        if is_df23_fork {
            // Deploy token split contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dst20_v2_deploy_info();

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;

            let (tx, receipt) =
                deploy_contract_tx(get_dst20_v2_contract().contract.init_bytecode, &base_fee)?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                logs_bloom,
            ));

            // Point proxy to DST20_v2
            let storage = vec![(
                IMPLEMENTATION_SLOT,
                h160_to_h256(get_dst20_v2_contract().fixed_address),
            )];

            executor.update_storage(address, storage)?;
        }

        template.backend.increase_tx_count();
        Ok(())
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn flush_state_to_db(&self) -> Result<()> {
        self.core.flush()?;
        self.storage.flush()
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
        mnview_ptr: usize,
    ) -> Result<BlockTemplate> {
        let (target_block, initial_state_root) = match self.storage.get_latest_block()? {
            None => (U256::zero(), GENESIS_STATE_ROOT), // Genesis block
            Some(block) => (
                block
                    .header
                    .number
                    .checked_add(U256::one())
                    .ok_or_else(|| format_err!("Block number overflow"))?,
                block.header.state_root,
            ),
        };

        let attrs = ain_cpp_imports::get_attribute_values(Some(mnview_ptr));
        let attr_block_gas_limit = attrs.block_gas_limit;
        let attr_block_gas_limit_factor = attrs.block_gas_target_factor;

        let block_difficulty = U256::from(difficulty);
        let (parent_hash, _) = self
            .block
            .get_latest_block_hash_and_number()?
            .unwrap_or_default(); // Safe since calculate_base_fee will default to INITIAL_BASE_FEE

        let block_base_fee_per_gas = self
            .block
            .calculate_base_fee(parent_hash, attr_block_gas_limit_factor)?;

        let block_gas_limit = U256::from(attr_block_gas_limit);
        let vicinity = Vicinity {
            beneficiary,
            block_number: target_block,
            timestamp,
            block_difficulty,
            block_gas_limit,
            block_base_fee_per_gas,
            block_randomness: None,
            ..Vicinity::default()
        };

        let ctx = BlockContext {
            parent_hash,
            dvm_block,
            mnview_ptr,
            attrs,
        };

        let backend = EVMBackend::from_root(
            initial_state_root,
            Arc::clone(&self.core.trie_store),
            Arc::clone(&self.storage),
            vicinity.clone(),
            None,
        )?;

        let template = BlockTemplate::new(vicinity, ctx, backend);
        Ok(template)
    }

    unsafe fn verify_tx_fees(&self, base_fee_per_gas: U256, tx: &ExecuteTx) -> Result<()> {
        if let ExecuteTx::SignedTx(signed_tx) = tx {
            let tx_gas_price = signed_tx.gas_price();
            if tx_gas_price < base_fee_per_gas {
                return Err(format_err!(
                    "tx gas price per gas is lower than block base fee per gas"
                )
                .into());
            }
        }
        Ok(())
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn push_tx_in_block_template(
        &self,
        template: &mut BlockTemplate,
        tx: ExecuteTx,
        hash: XHash,
    ) -> Result<()> {
        if template.is_genesis_block() {
            return Err(
                format_err!("genesis block, tx cannot be added into block template").into(),
            );
        }

        self.verify_tx_fees(template.get_block_base_fee_per_gas(), &tx)?;
        let tx_update = self.update_block_template_state_from_tx(template, tx.clone())?;
        template.add_tx(tx_update, hash)?;
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
        template: &BlockTemplate,
    ) -> Result<bool> {
        let backend = &template.backend;

        Ok(match backend.get_account(&address) {
            None => false,
            Some(account) => account.code_hash != H256::zero(),
        })
    }
}
