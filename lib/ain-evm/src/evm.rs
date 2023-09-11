use std::{path::PathBuf, sync::Arc};

use ain_contracts::{get_dst20_contract, get_transferdomain_contract, Contract, FixedContract};
use anyhow::format_err;
use ethereum::{Block, PartialHeader, ReceiptV3};
use ethereum_types::{Bloom, H160, H256, H64, U256};
use log::debug;

use crate::{
    backend::{EVMBackend, Vicinity},
    block::BlockService,
    contract::{
        bridge_dst20, dst20_contract, dst20_deploy_contract_tx, get_dst20_migration_txs,
        intrinsics_contract, reserve_dst20_namespace, reserve_intrinsics_namespace,
        transfer_domain_contract, transfer_domain_deploy_contract_tx, DST20BridgeInfo,
        DeployContractInfo,
    },
    core::{EVMCoreService, XHash},
    executor::{AinExecutor, TxResponse},
    fee::{calculate_gas_fee, calculate_prepay_gas_fee},
    filters::FilterService,
    log::LogService,
    receipt::ReceiptService,
    storage::{traits::BlockStorage, Storage},
    traits::{BridgeBackend, Executor},
    transaction::{
        system::{DST20Data, DeployContractData, SystemTx, TransferDirection, TransferDomainData},
        SignedTx,
    },
    trie::GENESIS_STATE_ROOT,
    txqueue::{BlockData, QueueTx},
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
        let datadir = ain_cpp_imports::get_datadir();
        let path = PathBuf::from(datadir).join("evm");
        if !path.exists() {
            std::fs::create_dir(&path)?
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
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
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
        let mut queue = tx_queue.data.lock().unwrap();

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
            .map_or(GENESIS_STATE_ROOT.parse().unwrap(), |block| {
                block.header.state_root
            });

        debug!("[construct_block] queue_id: {:?}", queue_id);
        debug!("[construct_block] beneficiary: {:?}", beneficiary);
        let (vicinity, parent_hash, current_block_number) = match parent_data {
            None => (
                Vicinity {
                    beneficiary,
                    timestamp: U256::from(timestamp),
                    block_number: U256::zero(),
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

            // Deploy counter contract on the first block
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = intrinsics_contract(executor.backend, dvm_block_number, current_block_number)?;

            debug!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();

            // Deploy transfer domain contract on the first block
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = transfer_domain_contract()?;

            debug!("deploying {:x?} bytecode {:?}", address, bytecode);
            executor.deploy_contract(address, bytecode, storage)?;
            executor.commit();
            let (tx, receipt) = transfer_domain_deploy_contract_tx(&base_fee)?;

            all_transactions.push(Box::new(tx));
            receipts_v3.push((receipt, Some(address)));
        } else {
            // Ensure that state root changes by updating counter contract storage
            let DeployContractInfo {
                address, storage, ..
            } = intrinsics_contract(executor.backend, dvm_block_number, current_block_number)?;
            executor.update_storage(address, storage)?;
            executor.commit();
        }

        debug!(
            "[construct_block] Processing {:?} transactions in queue",
            queue.transactions.len()
        );

        for queue_item in queue.transactions.clone() {
            let (tx, logs, (receipt, address)) = match queue_item.tx {
                QueueTx::SignedTx(signed_tx) => {
                    let nonce = executor.get_nonce(&signed_tx.sender);
                    if signed_tx.nonce() != nonce {
                        return Err(format_err!("EVM block rejected for invalid nonce. Address {} nonce {}, signed_tx nonce: {}", signed_tx.sender, nonce, signed_tx.nonce()).into());
                    }

                    let prepay_gas = calculate_prepay_gas_fee(&signed_tx)?;
                    let (
                        TxResponse {
                            exit_reason,
                            logs,
                            used_gas,
                            ..
                        },
                        receipt,
                    ) = executor.exec(&signed_tx, prepay_gas);
                    debug!(
                        "receipt : {:#?}, exit_reason {:#?} for signed_tx : {:#x}",
                        receipt,
                        exit_reason,
                        signed_tx.transaction.hash()
                    );
                    if !exit_reason.is_succeed() {
                        failed_transactions.push(queue_item.tx_hash);
                    }

                    let gas_fee = calculate_gas_fee(&signed_tx, U256::from(used_gas), base_fee)?;
                    total_gas_used += used_gas;
                    total_gas_fees += gas_fee;

                    (signed_tx, logs, (receipt, None))
                }
                QueueTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
                    signed_tx,
                    direction,
                })) => {
                    let to = signed_tx.to().unwrap();
                    let input = signed_tx.data();
                    let amount = U256::from_big_endian(&input[68..100]);
                    let native_hash = &queue_item.tx_hash;

                    debug!(
                        "[construct_block] Transfer domain: {} from address {:x?}, to address {:x?}, amount: {}, queue_id {}, tx hash {}",
                        direction, signed_tx.sender, to, amount, queue_id, queue_item.tx_hash
                    );

                    let FixedContract {
                        contract,
                        fixed_address,
                        ..
                    } = get_transferdomain_contract();
                    let mismatch = match executor.backend.get_account(&fixed_address) {
                        None => true,
                        Some(account) => account.code_hash != contract.codehash,
                    };
                    if mismatch {
                        debug!("[construct_block] EvmIn failed with as transferdomain account codehash mismatch");
                        failed_transactions.push(native_hash.clone());
                        continue;
                    }

                    if direction == TransferDirection::EvmIn {
                        if let Err(e) = executor.add_balance(fixed_address, amount) {
                            debug!("[construct_block] EvmIn failed with {e}");
                            failed_transactions.push(native_hash.clone());
                            continue;
                        }
                        executor.commit();
                    }

                    let (
                        TxResponse {
                            exit_reason, logs, ..
                        },
                        receipt,
                    ) = executor.exec(&signed_tx, U256::zero());

                    executor.commit();

                    debug!(
                        "receipt : {:#?}, exit_reason {:#?} for signed_tx : {:#x}, logs: {:x?}",
                        receipt,
                        exit_reason,
                        signed_tx.transaction.hash(),
                        logs
                    );
                    if !exit_reason.is_succeed() {
                        failed_transactions.push(native_hash.clone());
                    }

                    if direction == TransferDirection::EvmOut {
                        if let Err(e) = executor.sub_balance(signed_tx.sender, amount) {
                            debug!("[construct_block] EvmIn failed with {e}");
                            failed_transactions.push(queue_item.tx_hash);
                        }
                    }

                    (signed_tx, logs, (receipt, None))
                }
                QueueTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                    signed_tx,
                    contract_address,
                    direction,
                })) => {
                    let input = signed_tx.data();
                    let native_hash = &queue_item.tx_hash;
                    let amount = U256::from_big_endian(&input[100..132]);

                    debug!(
                        "[construct_block] DST20Bridge from {}, contract_address {}, amount {}, direction {}",
                        signed_tx.sender, contract_address, amount, direction
                    );

                    if direction == TransferDirection::EvmIn {
                        match bridge_dst20(
                            executor.backend,
                            contract_address,
                            signed_tx.sender,
                            amount,
                            direction,
                        ) {
                            Ok(DST20BridgeInfo { address, storage }) => {
                                if let Err(e) = executor.update_storage(address, storage) {
                                    debug!("[construct_block] EvmOut failed with {e}");
                                    failed_transactions.push(native_hash.clone());
                                }
                            }
                            Err(e) => {
                                debug!("[construct_block] EvmOut failed with {e}");
                                failed_transactions.push(native_hash.clone());
                            }
                        }
                        executor.commit();
                    }

                    let (
                        TxResponse {
                            exit_reason, logs, ..
                        },
                        receipt,
                    ) = executor.exec(&signed_tx, U256::zero());

                    executor.commit();

                    debug!(
                        "receipt : {:#?}, exit_reason {:#?} for signed_tx : {:#x}, logs: {:x?}",
                        receipt,
                        exit_reason,
                        signed_tx.transaction.hash(),
                        logs
                    );
                    if !exit_reason.is_succeed() {
                        failed_transactions.push(native_hash.clone());
                    }

                    if direction == TransferDirection::EvmOut {
                        match bridge_dst20(
                            executor.backend,
                            contract_address,
                            signed_tx.sender,
                            amount,
                            direction,
                        ) {
                            Ok(DST20BridgeInfo { address, storage }) => {
                                if let Err(e) = executor.update_storage(address, storage) {
                                    debug!("[construct_block] EvmOut failed with {e}");
                                    failed_transactions.push(native_hash.clone());
                                }
                            }
                            Err(e) => {
                                debug!("[construct_block] EvmOut failed with {e}");
                                failed_transactions.push(native_hash.clone());
                            }
                        }
                    }

                    (signed_tx, logs, (receipt, None))
                }
                QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
                    name,
                    symbol,
                    address,
                    token_id,
                })) => {
                    debug!(
                        "[construct_block] DeployContract for address {:x?}, name {}, symbol {}",
                        address, name, symbol
                    );

                    let DeployContractInfo {
                        address,
                        bytecode,
                        storage,
                    } = dst20_contract(executor.backend, address, &name, &symbol)?;

                    if let Err(e) = executor.deploy_contract(address, bytecode.clone(), storage) {
                        debug!("[construct_block] EvmOut failed with {e}");
                    }
                    let (tx, receipt) =
                        dst20_deploy_contract_tx(token_id, &base_fee, &name, &symbol)?;

                    (tx, Vec::new(), (receipt, Some(address)))
                }
            };

            all_transactions.push(tx);
            EVMCoreService::logs_bloom(logs, &mut logs_bloom);
            receipts_v3.push((receipt, address));

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

        let extra_data = format!("DFI: {}", dvm_block_number).into_bytes();
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

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn commit_queue(&self, queue_id: u64) -> Result<()> {
        {
            let tx_queue = self.core.tx_queues.get(queue_id)?;
            let queue = tx_queue.data.lock().unwrap();
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
        }
        self.core.tx_queues.remove(queue_id);

        Ok(())
    }

    pub fn verify_tx_fees(&self, tx: &str) -> Result<()> {
        debug!("[verify_tx_fees] raw transaction : {:#?}", tx);
        let signed_tx = SignedTx::try_from(tx)
            .map_err(|_| format_err!("Error: decoding raw tx to TransactionV2"))?;
        debug!(
            "[verify_tx_fees] TransactionV2 : {:#?}",
            signed_tx.transaction
        );

        let block_fee = self.block.calculate_next_block_base_fee()?;
        let tx_gas_price = signed_tx.gas_price();
        if tx_gas_price < block_fee {
            debug!("[verify_tx_fees] tx gas price is lower than block base fee");
            return Err(format_err!("tx gas price is lower than block base fee").into());
        }

        Ok(())
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn push_tx_in_queue(
        &self,
        queue_id: u64,
        tx: QueueTx,
        hash: XHash,
        gas_used: U256,
    ) -> Result<()> {
        self.core
            .tx_queues
            .push_in(queue_id, tx.clone(), hash, gas_used)?;

        if let QueueTx::SignedTx(signed_tx) = tx {
            self.filters.add_tx_to_filters(signed_tx.transaction.hash());
        }

        Ok(())
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
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
                let Contract { codehash, .. } = get_dst20_contract();
                if account.code_hash == codehash {
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

        let is_queued = self.core.tx_queues.get(queue_id)?.is_queued(deploy_tx);

        Ok(is_queued)
    }

    pub fn get_nonce(&self, address: H160) -> Result<U256> {
        let backend = self.core.get_latest_block_backend()?;
        let nonce = backend.get_nonce(&address);
        Ok(nonce)
    }
}
