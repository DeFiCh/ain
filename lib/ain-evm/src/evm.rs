use std::{path::PathBuf, sync::Arc};

use ain_contracts::{
    get_dfi_instrinics_registry_contract, get_dfi_intrinsics_v1_contract, get_dst20_v1_contract,
    get_transfer_domain_contract, get_transfer_domain_v1_contract,
};
use anyhow::format_err;
use ethereum::{Block, PartialHeader};
use ethereum_types::{Bloom, H160, H256, H64, U256};
use log::{debug, trace};
use parking_lot::Mutex;
use tokio::sync::{
    mpsc::{self, UnboundedReceiver, UnboundedSender},
    RwLock,
};

use crate::{
    backend::{EVMBackendMut, Vicinity},
    block::BlockService,
    blocktemplate::{BlockData, BlockTemplate, ReceiptAndOptionalContractAddress, TemplateTxItem},
    contract::{
        deploy_contract_tx, dfi_intrinsics_registry_deploy_info, dfi_intrinsics_v1_deploy_info,
        dst20_v1_deploy_info, get_dst20_migration_txs, reserve_dst20_namespace,
        reserve_intrinsics_namespace, transfer_domain_deploy_info,
        transfer_domain_v1_contract_deploy_info, DeployContractInfo,
    },
    core::{EVMCoreService, XHash},
    executor::{execute_tx, ExecuteTx},
    filters::FilterService,
    log::{LogService, Notification},
    receipt::ReceiptService,
    storage::{traits::BlockStorage, Storage},
    transaction::SignedTx,
    trie::{TrieMut, GENESIS_STATE_ROOT},
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
                core: EVMCoreService::restore(Arc::clone(&storage), path)?,
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
        template_ptr: *mut BlockTemplate,
        is_miner: bool,
    ) -> Result<FinalizedBlockInfo> {
        let mut block_template = unsafe { &mut *template_ptr };
        let template = &mut *block_template;
        let mut backend = unsafe { &mut *template.backend };

        let logs_bloom = template.get_latest_logs_bloom();

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

        debug!("[construct_block] vicinity: {:?}", template.vicinity);

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
        debug!(
            "[construct_block] Total burnt fees : {:#?}",
            total_burnt_fees
        );
        debug!(
            "[construct_block] Total priority fees : {:#?}",
            total_priority_fees
        );

        debug!("[construct_block] adding balance");
        // burn base fee and pay priority fee to miner
        backend.add_balance(beneficiary, total_priority_fees)?;

        debug!("[construct_block] getting root");
        let root = backend.root();
        debug!("[construct_block] root : {:?}", root);

        let extra_data = format!("DFI: {}", template.dvm_block).into_bytes();
        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary,
                state_root: root,
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
        debug!("generating receipts");
        let receipts = self.receipt.generate_receipts(
            &all_transactions,
            receipts_v3.clone(),
            block.header.hash(),
            block.header.number,
            base_fee,
        )?;
        template.block_data = Some(BlockData { block, receipts });

        debug!("returning out of construct_block");
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
    pub unsafe fn commit_block(&self, template_ptr: *mut BlockTemplate) -> Result<()> {
        debug!("[commit_block]");
        let template = unsafe { &*template_ptr };
        {
            let Some(BlockData { block, receipts }) = template.block_data.clone() else {
                debug!("Coulndt get block data");
                return Err(format_err!("no constructed EVM block exist in template id").into());
            };

            debug!(
                "[commit_block] Finalizing block number {:#x}, state_root {:#x}",
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

            debug!("[commit_block] finish committing to storage");
        }
        self.core.clear_account_nonce();
        self.core.clear_transaction_cache();

        Ok(())
    }

    unsafe fn update_block_template_state_from_tx<'a>(
        &self,
        template: &mut BlockTemplate<'a>,
        tx: ExecuteTx,
    ) -> Result<ExecTxState> {
        let mut backend = unsafe { &mut *template.backend };

        let mut logs_bloom = template.get_latest_logs_bloom();

        let (parent_hash, _) = self
            .block
            .get_latest_block_hash_and_number()?
            .unwrap_or_default(); // Safe since calculate_base_fee will default to INITIAL_BASE_FEE
        let base_fee = self.block.calculate_base_fee(parent_hash)?;
        debug!(
            "[update_block_template_state_from_tx] Block base fee: {}",
            base_fee
        );
        debug!("update_block_template_state_from_tx: {:?}", tx);

        backend.update_vicinity_with_gas_used(template.total_gas_used);
        let apply_tx = execute_tx(&mut backend, tx, base_fee)?;
        EVMCoreService::logs_bloom(apply_tx.logs, &mut logs_bloom);

        Ok(ExecTxState {
            tx: apply_tx.tx,
            receipt: apply_tx.receipt,
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
        template_ptr: *mut BlockTemplate,
        mnview_ptr: usize,
    ) -> Result<()> {
        let mut block_template = unsafe { &mut *template_ptr };
        let template = &mut *block_template;

        // reserve DST20 namespace
        let is_evm_genesis_block = template.get_block_number() == U256::zero();
        let mut logs_bloom = template.get_latest_logs_bloom();

        let base_fee = template.vicinity.block_base_fee_per_gas;
        let vicinity = template.vicinity.clone();

        let mut backend = unsafe { &mut *template.backend };

        debug!(
            "[update_block_template_state_from_tx] Block base fee: {}",
            base_fee
        );

        if is_evm_genesis_block {
            reserve_dst20_namespace(&mut backend)?;
            reserve_intrinsics_namespace(&mut backend)?;

            // Deploy DFIIntrinsicsRegistry contract
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = dfi_intrinsics_registry_deploy_info(get_dfi_intrinsics_v1_contract().fixed_address);

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            backend.deploy_contract(&address, bytecode.0, storage)?;

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
            } = dfi_intrinsics_v1_deploy_info(template.dvm_block, template.vicinity.block_number)?;

            trace!("deploying {:x?} bytecode {:?}", address, bytecode);
            backend.deploy_contract(&address, bytecode.0, storage)?;

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
            backend.deploy_contract(&address, bytecode.0, storage)?;

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
            backend.deploy_contract(&address, bytecode.0, storage)?;

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

            backend.deploy_contract(&address, bytecode.0, storage)?;

            // DST20 implementation contract deployment TX
            let (tx, receipt) =
                deploy_contract_tx(get_dst20_v1_contract().contract.init_bytecode, &base_fee)?;
            template.transactions.push(TemplateTxItem::new_system_tx(
                Box::new(tx),
                (receipt, Some(address)),
                logs_bloom,
            ));

            debug!("Getting deploy DST20 migration TX");
            // Deploy DST20 migration TX
            debug!("mnview_ptr : {:?}", mnview_ptr);
            let migration_txs = get_dst20_migration_txs(mnview_ptr)?;
            debug!("migration_txs : {:?}", migration_txs);
            for exec_tx in migration_txs.clone() {
                let apply_result = execute_tx(&mut backend, exec_tx, base_fee)?;
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
            } = dfi_intrinsics_v1_deploy_info(template.dvm_block, template.vicinity.block_number)?;

            backend.update_storage(&address, storage)?;
        }
        debug!("[update_state_in_block_template] done");
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
    pub unsafe fn create_block_template<'a>(
        &self,
        trie: *mut TrieMut<'a>,
        dvm_block: u64,
        beneficiary: H160,
        difficulty: u32,
        timestamp: u64,
    ) -> Result<BlockTemplate<'a>> {
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

        let block_difficulty = U256::from(difficulty);
        let (parent_hash, _) = self
            .block
            .get_latest_block_hash_and_number()?
            .unwrap_or_default(); // Safe since calculate_base_fee will default to INITIAL_BASE_FEE
        let block_base_fee_per_gas = self.block.calculate_base_fee(parent_hash)?;
        let block_gas_limit = U256::from(self.storage.get_attributes_or_default()?.block_gas_limit);

        let vicinity = Vicinity {
            beneficiary,
            block_number: target_block,
            timestamp: U256::from(timestamp),
            total_gas_used: U256::zero(),
            block_difficulty,
            block_gas_limit,
            block_base_fee_per_gas,
            block_randomness: None,
            ..Vicinity::default()
        };

        let mut backend =
            EVMBackendMut::from_root(trie, Arc::clone(&self.storage), vicinity.clone());
        let template = BlockTemplate::new(
            vicinity,
            parent_hash,
            dvm_block,
            timestamp,
            Box::into_raw(Box::new(backend)),
        );
        Ok(template)
    }

    unsafe fn verify_tx_fees_in_block_template<'a>(
        &self,
        template: &mut BlockTemplate<'a>,
        tx: &ExecuteTx,
    ) -> Result<()> {
        if let ExecuteTx::SignedTx(signed_tx) = tx {
            let base_fee_per_gas = template.get_block_base_fee_per_gas();

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
        template_ptr: *mut BlockTemplate,
        tx: ExecuteTx,
        hash: XHash,
    ) -> Result<()> {
        let mut block_template = unsafe { &mut *template_ptr };
        let mut template = &mut *block_template;

        debug!("push_tx_in_block_template");
        debug!("push_tx_in_block_template");
        self.verify_tx_fees_in_block_template(&mut template, &tx)?;
        debug!("push_tx_in_block_template verify_tx_fees_in_block_template");
        debug!("push_tx_in_block_template verify_tx_fees_in_block_template");
        let tx_update = { self.update_block_template_state_from_tx(&mut template, tx.clone())? };
        debug!("push_tx_in_block_template update_block_template_state_from_tx");
        debug!("push_tx_in_block_template update_block_template_state_from_tx");
        let tx_hash = tx_update.tx.hash();
        debug!("push_tx_in_block_template hash");
        debug!("push_tx_in_block_template hash");

        template.add_tx(tx_update, hash)?;
        debug!("push_tx_in_block_template add_tx");
        debug!("push_tx_in_block_template add_tx");
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
        template_ptr: *mut BlockTemplate,
    ) -> Result<bool> {
        let mut template = unsafe { &mut *template_ptr };
        let backend = unsafe { &mut *template.backend };
        Ok(match backend.get_account(&address) {
            None => false,
            Some(account) => account.code_hash != H256::zero(),
        })
    }
}
