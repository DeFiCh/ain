use crate::backend::{EVMBackend, Vicinity};
use crate::block::BlockService;
use crate::core::{EVMCoreService, EVMError, NativeTxHash, MAX_GAS_PER_BLOCK};
use crate::executor::{AinExecutor, TxResponse};
use crate::fee::{calculate_gas_fee, calculate_prepay_gas, get_tx_max_gas_price};
use crate::filters::FilterService;
use crate::log::LogService;
use crate::receipt::ReceiptService;
use crate::storage::traits::BlockStorage;
use crate::storage::Storage;
use crate::traits::Executor;
use crate::transaction::bridge::{BalanceUpdate, BridgeTx};
use crate::transaction::SignedTx;
use crate::trie::GENESIS_STATE_ROOT;
use crate::txqueue::QueueTx;

use ethereum::{Block, PartialHeader, ReceiptV3, TransactionV2};
use ethereum_types::{Bloom, H160, H64, U256};

use crate::bytes::Bytes;
use crate::services::SERVICES;
use ain_contracts::{Contracts, CONTRACT_ADDRESSES};
use anyhow::anyhow;
use hex::FromHex;
use log::debug;
use primitive_types::H256;
use std::error::Error;
use std::path::PathBuf;
use std::sync::Arc;

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
}

pub struct CounterContractInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
    pub bytecode: Bytes,
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
    pub fn new() -> Result<Self, anyhow::Error> {
        let services = if let Some(path) = ain_cpp_imports::get_state_input_json() {
            if ain_cpp_imports::get_network() != "regtest" {
                return Err(anyhow!(
                    "Loading a genesis from JSON file is restricted to regtest network"
                ));
            }
            let storage = Arc::new(Storage::new());
            Self {
                core: EVMCoreService::new_from_json(Arc::clone(&storage), PathBuf::from(path)),
                block: BlockService::new(Arc::clone(&storage)),
                receipt: ReceiptService::new(Arc::clone(&storage)),
                logs: LogService::new(Arc::clone(&storage)),
                filters: FilterService::new(),
                storage,
            }
        } else {
            let storage = Arc::new(Storage::restore());
            Self {
                core: EVMCoreService::restore(Arc::clone(&storage)),
                block: BlockService::new(Arc::clone(&storage)),
                receipt: ReceiptService::new(Arc::clone(&storage)),
                logs: LogService::new(Arc::clone(&storage)),
                filters: FilterService::new(),
                storage,
            }
        };

        Ok(services)
    }

    pub fn finalize_block(
        &self,
        context: u64,
        update_state: bool,
        difficulty: u32,
        beneficiary: H160,
        timestamp: u64,
    ) -> Result<FinalizedBlockInfo, Box<dyn Error>> {
        let mut all_transactions = Vec::with_capacity(self.core.tx_queues.len(context));
        let mut failed_transactions = Vec::with_capacity(self.core.tx_queues.len(context));
        let mut receipts_v3: Vec<ReceiptV3> = Vec::with_capacity(self.core.tx_queues.len(context));
        let mut total_gas_used = 0u64;
        let mut total_gas_fees = U256::zero();
        let mut logs_bloom: Bloom = Bloom::default();

        let parent_data = self.block.get_latest_block_hash_and_number();
        let state_root = self
            .storage
            .get_latest_block()
            .map_or(GENESIS_STATE_ROOT.parse().unwrap(), |block| {
                block.header.state_root
            });

        let (vicinity, parent_hash, current_block_number) = match parent_data {
            None => (
                Vicinity {
                    beneficiary,
                    timestamp: U256::from(timestamp),
                    block_number: U256::zero(),
                    ..Default::default()
                },
                H256::zero(),
                U256::zero(),
            ),
            Some((hash, number)) => (
                Vicinity {
                    beneficiary,
                    timestamp: U256::from(timestamp),
                    block_number: number + 1,
                    ..Default::default()
                },
                hash,
                number + 1,
            ),
        };

        let base_fee = self.block.calculate_base_fee(parent_hash);

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.core.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )?;

        let mut executor = AinExecutor::new(&mut backend);

        if current_block_number == U256::zero() {
            let CounterContractInfo {
                address,
                storage,
                bytecode,
            } = EVMServices::counter_contract().unwrap();
            executor
                .deploy_contract(address, bytecode, storage)
                .unwrap();
        } else {
            let CounterContractInfo {
                address, storage, ..
            } = EVMServices::counter_contract().unwrap();
            executor.update_storage(address, storage).unwrap();
        }

        for (queue_tx, hash) in self.core.tx_queues.get_cloned_vec(context) {
            match queue_tx {
                QueueTx::SignedTx(signed_tx) => {
                    if ain_cpp_imports::past_changi_intermediate_height_4_height() {
                        let nonce = executor.get_nonce(&signed_tx.sender);
                        if signed_tx.nonce() != nonce {
                            return Err(anyhow!("EVM block rejected for invalid nonce. Address {} nonce {}, signed_tx nonce: {}", signed_tx.sender, nonce, signed_tx.nonce()).into());
                        }
                    }

                    let prepay_gas = calculate_prepay_gas(&signed_tx)?;
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
                        "receipt : {:#?} for signed_tx : {:#x}",
                        receipt,
                        signed_tx.transaction.hash()
                    );

                    if !exit_reason.is_succeed() {
                        failed_transactions.push(hex::encode(hash));
                    }

                    let gas_fee = calculate_gas_fee(&signed_tx, U256::from(used_gas), base_fee)?;
                    total_gas_used += used_gas;
                    total_gas_fees += gas_fee;

                    all_transactions.push(signed_tx.clone());
                    EVMCoreService::logs_bloom(logs, &mut logs_bloom);
                    receipts_v3.push(receipt);
                }
                QueueTx::BridgeTx(BridgeTx::EvmIn(BalanceUpdate { address, amount })) => {
                    debug!(
                        "[finalize_block] EvmIn for address {:x?}, amount: {}, context {}",
                        address, amount, context
                    );
                    if let Err(e) = executor.add_balance(address, amount) {
                        debug!("[finalize_block] EvmIn failed with {e}");
                        failed_transactions.push(hex::encode(hash));
                    }
                }
                QueueTx::BridgeTx(BridgeTx::EvmOut(BalanceUpdate { address, amount })) => {
                    debug!(
                        "[finalize_block] EvmOut for address {}, amount: {}",
                        address, amount
                    );

                    if let Err(e) = executor.sub_balance(address, amount) {
                        debug!("[finalize_block] EvmOut failed with {e}");
                        failed_transactions.push(hex::encode(hash));
                    }
                }
            }

            executor.commit();
        }

        if update_state {
            self.core.tx_queues.remove(context);
        }

        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary,
                state_root: if update_state {
                    backend.commit()
                } else {
                    backend.root()
                },
                receipts_root: ReceiptService::get_receipts_root(&receipts_v3),
                logs_bloom,
                difficulty: U256::from(difficulty),
                number: current_block_number,
                gas_limit: MAX_GAS_PER_BLOCK,
                gas_used: U256::from(total_gas_used),
                timestamp,
                extra_data: Vec::default(),
                mix_hash: H256::default(),
                nonce: H64::default(),
            },
            all_transactions
                .iter()
                .map(|signed_tx| signed_tx.transaction.clone())
                .collect(),
            Vec::new(),
        );

        let receipts = self.receipt.generate_receipts(
            &all_transactions,
            receipts_v3,
            block.header.hash(),
            block.header.number,
        );

        if update_state {
            debug!(
                "[finalize_block] Finalizing block number {:#x}, state_root {:#x}",
                block.header.number, block.header.state_root
            );

            self.block.connect_block(block.clone(), base_fee);
            self.logs
                .generate_logs_from_receipts(&receipts, block.header.number);
            self.receipt.put_receipts(receipts);
            self.filters.add_block_to_filters(block.header.hash());
        }

        if ain_cpp_imports::past_changi_intermediate_height_4_height() {
            let total_burnt_fees = U256::from(total_gas_used) * base_fee;
            let total_priority_fees = total_gas_fees - total_burnt_fees;
            debug!(
                "[finalize_block] Total burnt fees : {:#?}",
                total_burnt_fees
            );
            debug!(
                "[finalize_block] Total priority fees : {:#?}",
                total_priority_fees
            );
            Ok(FinalizedBlockInfo {
                block_hash: *block.header.hash().as_fixed_bytes(),
                failed_transactions,
                total_burnt_fees,
                total_priority_fees,
            })
        } else {
            Ok(FinalizedBlockInfo {
                block_hash: *block.header.hash().as_fixed_bytes(),
                failed_transactions,
                total_burnt_fees: U256::from(total_gas_used),
                total_priority_fees: U256::zero(),
            })
        }
    }

    pub fn verify_tx_fees(&self, tx: &str, use_context: bool) -> Result<(), Box<dyn Error>> {
        debug!("[verify_tx_fees] raw transaction : {:#?}", tx);
        let buffer = <Vec<u8>>::from_hex(tx)?;
        let tx: TransactionV2 = ethereum::EnvelopedDecodable::decode(&buffer)
            .map_err(|_| anyhow!("Error: decoding raw tx to TransactionV2"))?;
        debug!("[verify_tx_fees] TransactionV2 : {:#?}", tx);
        let signed_tx: SignedTx = tx.try_into()?;

        if ain_cpp_imports::past_changi_intermediate_height_4_height() {
            let mut block_fees = self.block.calculate_base_fee(H256::zero());
            if use_context {
                block_fees = self.block.calculate_next_block_base_fee();
            }

            let tx_gas_price = get_tx_max_gas_price(&signed_tx);
            if tx_gas_price < block_fees {
                debug!("[verify_tx_fees] tx gas price is lower than block base fee");
                return Err(anyhow!("tx gas price is lower than block base fee").into());
            }
        }

        Ok(())
    }

    pub fn queue_tx(
        &self,
        context: u64,
        tx: QueueTx,
        hash: NativeTxHash,
        gas_used: u64,
    ) -> Result<(), EVMError> {
        self.core
            .tx_queues
            .queue_tx(context, tx.clone(), hash, gas_used)?;

        if let QueueTx::SignedTx(signed_tx) = tx {
            self.filters.add_tx_to_filters(signed_tx.transaction.hash())
        }

        Ok(())
    }

    pub fn counter_contract() -> Result<CounterContractInfo, Box<dyn Error>> {
        let address = *CONTRACT_ADDRESSES.get(&Contracts::CounterContract).unwrap();
        let bytecode = ain_contracts::get_counter_bytecode()?;
        let (_, latest_block_number) = SERVICES
            .evm
            .block
            .get_latest_block_hash_and_number()
            .unwrap_or_default();
        let count = U256::from(
            SERVICES
                .evm
                .core
                .get_storage_at(address, U256::one(), latest_block_number)?
                .unwrap_or_default()
                .as_slice(),
        );

        debug!("Count: {:#x}", count + U256::one());

        Ok(CounterContractInfo {
            address,
            bytecode: Bytes::from(bytecode),
            storage: vec![(
                H256::from_low_u64_be(1),
                ain_contracts::u256_to_h256(count + U256::one()),
            )],
        })
    }
}
