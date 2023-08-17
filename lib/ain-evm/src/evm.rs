use std::path::PathBuf;
use std::sync::Arc;

use ain_contracts::{Contracts, CONTRACT_ADDRESSES};
use anyhow::format_err;
use ethereum::{
    Block, EIP1559ReceiptData, LegacyTransaction, PartialHeader, ReceiptV3, TransactionAction,
    TransactionSignature, TransactionV2,
};
use ethereum_types::{Bloom, H160, H64, U256};
use log::debug;
use primitive_types::H256;

use crate::backend::{EVMBackend, Vicinity};
use crate::block::BlockService;
use crate::bytes::Bytes;
use crate::core::{EVMCoreService, NativeTxHash};
use crate::executor::{AinExecutor, TxResponse};
use crate::fee::{calculate_gas_fee, calculate_prepay_gas_fee};
use crate::filters::FilterService;
use crate::log::LogService;
use crate::receipt::ReceiptService;
use crate::services::SERVICES;
use crate::storage::traits::BlockStorage;
use crate::storage::Storage;
use crate::traits::Executor;
use crate::transaction::system::{BalanceUpdate, DST20Data, DeployContractData, SystemTx};
use crate::transaction::{SignedTx, LOWER_H256};
use crate::trie::GENESIS_STATE_ROOT;
use crate::txqueue::{BlockData, QueueTx, QueueTxItem};
use crate::Result;

pub struct EVMServices {
    pub core: EVMCoreService,
    pub block: BlockService,
    pub receipt: ReceiptService,
    pub logs: LogService,
    pub filters: FilterService,
    pub storage: Arc<Storage>,
}

pub struct FinalizedBlockInfo {
    pub block_hash: [u8; 32],
    pub failed_transactions: Vec<String>,
    pub total_burnt_fees: U256,
    pub total_priority_fees: U256,
    pub block_number: U256,
}

pub struct DeployContractInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
    pub bytecode: Bytes,
}

pub struct DST20BridgeInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
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

        let is_evm_genesis_block = queue.target_block == U256::zero();
        if is_evm_genesis_block {
            let migration_txs = get_dst20_migration_txs(mnview_ptr)?;
            queue.transactions.extend(migration_txs.into_iter())
        }

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

        // Ensure that state root changes by updating counter contract storage
        if current_block_number == U256::zero() {
            // reserve DST20 namespace
            self.reserve_dst20_namespace(&mut executor)?;

            // Deploy contract on the first block
            let DeployContractInfo {
                address,
                storage,
                bytecode,
            } = EVMServices::counter_contract(dvm_block_number, current_block_number)?;
            executor.deploy_contract(address, bytecode, storage)?;
        } else {
            let DeployContractInfo {
                address, storage, ..
            } = EVMServices::counter_contract(dvm_block_number, current_block_number)?;
            executor.update_storage(address, storage)?;
        }
        for (idx, queue_item) in queue.transactions.clone().into_iter().enumerate() {
            match queue_item.tx {
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
                        failed_transactions.push(hex::encode(queue_item.tx_hash));
                    }

                    let gas_fee = calculate_gas_fee(&signed_tx, U256::from(used_gas), base_fee)?;
                    total_gas_used += used_gas;
                    total_gas_fees += gas_fee;

                    all_transactions.push(signed_tx.clone());
                    EVMCoreService::logs_bloom(logs, &mut logs_bloom);
                    receipts_v3.push((receipt, None));
                }
                QueueTx::SystemTx(SystemTx::EvmIn(BalanceUpdate { address, amount })) => {
                    debug!(
                        "[construct_block] EvmIn for address {:x?}, amount: {}, queue_id {}",
                        address, amount, queue_id
                    );
                    if let Err(e) = executor.add_balance(address, amount) {
                        debug!("[construct_block] EvmIn failed with {e}");
                        failed_transactions.push(hex::encode(queue_item.tx_hash));
                    }
                }
                QueueTx::SystemTx(SystemTx::EvmOut(BalanceUpdate { address, amount })) => {
                    debug!(
                        "[construct_block] EvmOut for address {}, amount: {}",
                        address, amount
                    );

                    if let Err(e) = executor.sub_balance(address, amount) {
                        debug!("[construct_block] EvmOut failed with {e}");
                        failed_transactions.push(hex::encode(queue_item.tx_hash));
                    }
                }
                QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
                    name,
                    symbol,
                    address,
                    token_id,
                })) => {
                    debug!(
                        "[construct_block] DeployContract for address {}, name {}, symbol {}",
                        address, name, symbol
                    );

                    let DeployContractInfo {
                        address,
                        bytecode,
                        storage,
                    } = EVMServices::dst20_contract(&mut executor, address, name, symbol)?;

                    if let Err(e) = executor.deploy_contract(address, bytecode.clone(), storage) {
                        debug!("[construct_block] EvmOut failed with {e}");
                    }
                    let (tx, receipt) = create_deploy_contract_tx(
                        token_id,
                        current_block_number,
                        &base_fee,
                        bytecode.into_vec(),
                    )?;

                    all_transactions.push(Box::new(tx));
                    receipts_v3.push((receipt, Some(address)));
                }
                QueueTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                    to,
                    contract,
                    amount,
                    out,
                })) => {
                    debug!(
                        "[construct_block] DST20Bridge for to {}, contract {}, amount {}, out {}",
                        to, contract, amount, out
                    );

                    match EVMServices::bridge_dst20(&mut executor, contract, to, amount, out) {
                        Ok(DST20BridgeInfo { address, storage }) => {
                            if let Err(e) = executor.update_storage(address, storage) {
                                debug!("[construct_block] EvmOut failed with {e}");
                                failed_transactions.push(hex::encode(queue_item.tx_hash));
                            }
                        }
                        Err(e) => {
                            debug!("[construct_block] EvmOut failed with {e}");
                            failed_transactions.push(hex::encode(queue_item.tx_hash));
                        }
                    }
                }
            }

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

        if (total_burnt_fees + total_priority_fees) != queue.total_fees {
            return Err(format_err!("EVM block rejected because block total fees != (burnt fees + priority fees). Burnt fees: {}, priority fees: {}, total fees: {}", total_burnt_fees, total_priority_fees, queue.total_fees).into());
        }

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

        let block_hash = *block.header.hash().as_fixed_bytes();
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
        hash: NativeTxHash,
        gas_used: U256,
    ) -> Result<()> {
        let parent_data = self.block.get_latest_block_hash_and_number()?;
        let parent_hash = match parent_data {
            Some((hash, _)) => hash,
            None => H256::zero(),
        };
        let base_fee = self.block.calculate_base_fee(parent_hash)?;

        self.core
            .tx_queues
            .push_in(queue_id, tx.clone(), hash, gas_used, base_fee)?;

        if let QueueTx::SignedTx(signed_tx) = tx {
            self.filters.add_tx_to_filters(signed_tx.transaction.hash());
        }

        Ok(())
    }

    /// Returns address, bytecode and storage with incremented count for the counter contract
    pub fn counter_contract(
        dvm_block_number: u64,
        evm_block_number: U256,
    ) -> Result<DeployContractInfo> {
        let address = *CONTRACT_ADDRESSES.get(&Contracts::CounterContract).unwrap();
        let bytecode = ain_contracts::get_counter_bytecode()?;
        let count = SERVICES
            .evm
            .core
            .get_latest_contract_storage(address, ain_contracts::u256_to_h256(U256::one()))?;

        debug!("Count: {:#x}", count + U256::one());

        Ok(DeployContractInfo {
            address,
            bytecode: Bytes::from(bytecode),
            storage: vec![
                (
                    H256::from_low_u64_be(0),
                    ain_contracts::u256_to_h256(U256::one()),
                ),
                (
                    H256::from_low_u64_be(1),
                    ain_contracts::u256_to_h256(evm_block_number),
                ),
                (
                    H256::from_low_u64_be(2),
                    ain_contracts::u256_to_h256(U256::from(dvm_block_number)),
                ),
            ],
        })
    }

    pub fn dst20_contract(
        executor: &mut AinExecutor,
        address: H160,
        name: String,
        symbol: String,
    ) -> Result<DeployContractInfo> {
        match executor.backend.get_account(&address) {
            None => {}
            Some(account) => {
                if account.code_hash != ain_contracts::get_system_reserved_codehash()? {
                    return Err(format_err!("Token address is already in use").into());
                }
            }
        }

        let bytecode = ain_contracts::get_dst20_bytecode()?;
        let storage = vec![
            (
                H256::from_low_u64_be(3),
                ain_contracts::get_abi_encoded_string(name.as_str()),
            ),
            (
                H256::from_low_u64_be(4),
                ain_contracts::get_abi_encoded_string(symbol.as_str()),
            ),
        ];

        Ok(DeployContractInfo {
            address,
            bytecode: Bytes::from(bytecode),
            storage,
        })
    }

    pub fn bridge_dst20(
        executor: &mut AinExecutor,
        contract: H160,
        to: H160,
        amount: U256,
        out: bool,
    ) -> Result<DST20BridgeInfo> {
        // check if code of address matches DST20 bytecode
        let account = executor
            .backend
            .get_account(&contract)
            .ok_or_else(|| format_err!("DST20 token address is not a contract"))?;

        if account.code_hash != ain_contracts::get_dst20_codehash()? {
            return Err(format_err!("DST20 token code is not valid").into());
        }

        let storage_index = ain_contracts::get_address_storage_index(to);
        let balance = executor
            .backend
            .get_contract_storage(contract, storage_index.as_bytes())?;

        let total_supply_index = H256::from_low_u64_be(2);
        let total_supply = executor
            .backend
            .get_contract_storage(contract, total_supply_index.as_bytes())?;

        let (new_balance, new_total_supply) = if out {
            (
                balance.checked_sub(amount),
                total_supply.checked_sub(amount),
            )
        } else {
            (
                balance.checked_add(amount),
                total_supply.checked_add(amount),
            )
        };

        let new_balance = new_balance.ok_or_else(|| format_err!("Balance overflow/underflow"))?;
        let new_total_supply =
            new_total_supply.ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

        Ok(DST20BridgeInfo {
            address: contract,
            storage: vec![
                (storage_index, ain_contracts::u256_to_h256(new_balance)),
                (
                    total_supply_index,
                    ain_contracts::u256_to_h256(new_total_supply),
                ),
            ],
        })
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
                if account.code_hash == ain_contracts::get_dst20_codehash()? {
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

    pub fn reserve_dst20_namespace(&self, executor: &mut AinExecutor) -> Result<()> {
        let bytecode = ain_contracts::get_system_reserved_bytecode()?;
        let addresses = (1..=1024)
            .map(|token_id| ain_contracts::dst20_address_from_token_id(token_id).unwrap())
            .collect::<Vec<H160>>();

        for address in addresses {
            debug!("Deploying address to {:#?}", address);
            executor.deploy_contract(address, bytecode.clone().into(), Vec::new())?;
        }

        Ok(())
    }
}

fn create_deploy_contract_tx(
    token_id: u64,
    block_number: U256,
    base_fee: &U256,
    bytecode: Vec<u8>,
) -> Result<(SignedTx, ReceiptV3)> {
    let tx = TransactionV2::Legacy(LegacyTransaction {
        nonce: U256::from(token_id),
        gas_price: base_fee.clone(),
        gas_limit: U256::from(u64::MAX),
        action: TransactionAction::Create,
        value: U256::zero(),
        input: bytecode,
        signature: TransactionSignature::new(27, LOWER_H256, LOWER_H256)
            .ok_or(format_err!("Invalid transaction signature format"))?,
    })
    .try_into()?;

    let receipt = ReceiptV3::Legacy(EIP1559ReceiptData {
        status_code: 1u8,
        used_gas: U256::zero(),
        logs_bloom: Bloom::default(),
        logs: Vec::new(),
    });

    Ok((tx, receipt))
}

fn get_dst20_migration_txs(mnview_ptr: usize) -> Result<Vec<QueueTxItem>> {
    let mut txs = Vec::new();
    for token in ain_cpp_imports::get_dst20_tokens(mnview_ptr) {
        let address = ain_contracts::dst20_address_from_token_id(token.id)?;
        debug!("Deploying to address {:#?}", address);

        let tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
            name: token.name,
            symbol: token.symbol,
            token_id: token.id,
            address,
        }));
        txs.push(QueueTxItem {
            tx,
            tx_hash: Default::default(),
            tx_fee: U256::zero(),
            gas_used: U256::zero(),
        });
    }
    Ok(txs)
}
