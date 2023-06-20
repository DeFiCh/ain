use crate::backend::{EVMBackend, Vicinity};
use crate::block::BlockHandler;
use crate::evm::{EVMHandler, MAX_GAS_PER_BLOCK};
use crate::executor::{AinExecutor, TxResponse};
use crate::log::LogHandler;
use crate::receipt::ReceiptHandler;
use crate::storage::traits::BlockStorage;
use crate::storage::Storage;
use crate::traits::Executor;
use crate::transaction::bridge::{BalanceUpdate, BridgeTx};
use crate::trie::GENESIS_STATE_ROOT;
use crate::tx_queue::QueueTx;

use anyhow::anyhow;
use ethereum::{Block, PartialHeader, ReceiptV3};
use ethereum_types::{Bloom, H160, H64, U256};
use log::debug;
use primitive_types::H256;
use std::error::Error;
use std::path::PathBuf;
use std::sync::Arc;

pub struct Handlers {
    pub evm: EVMHandler,
    pub block: BlockHandler,
    pub receipt: ReceiptHandler,
    pub logs: LogHandler,
    pub storage: Arc<Storage>,
}

impl Handlers {
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
        if let Some(path) = ain_cpp_imports::get_state_input_json() {
            if ain_cpp_imports::get_network() != "regtest" {
                return Err(anyhow!(
                    "Loading a genesis from JSON file is restricted to regtest network"
                ));
            }
            let storage = Arc::new(Storage::new());
            Ok(Self {
                evm: EVMHandler::new_from_json(Arc::clone(&storage), PathBuf::from(path)),
                block: BlockHandler::new(Arc::clone(&storage)),
                receipt: ReceiptHandler::new(Arc::clone(&storage)),
                logs: LogHandler::new(Arc::clone(&storage)),
                storage,
            })
        } else {
            let storage = Arc::new(Storage::restore());
            Ok(Self {
                evm: EVMHandler::restore(Arc::clone(&storage)),
                block: BlockHandler::new(Arc::clone(&storage)),
                receipt: ReceiptHandler::new(Arc::clone(&storage)),
                logs: LogHandler::new(Arc::clone(&storage)),
                storage,
            })
        }
    }

    pub fn finalize_block(
        &self,
        context: u64,
        update_state: bool,
        difficulty: u32,
        beneficiary: H160,
        timestamp: u64,
    ) -> Result<([u8; 32], Vec<String>, u64), Box<dyn Error>> {
        let mut all_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut evicted_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut receipts_v3: Vec<ReceiptV3> = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut gas_used = 0u64;
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

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.evm.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )?;

        let mut executor = AinExecutor::new(&mut backend);

        for (queue_tx, hash) in self.evm.tx_queues.get_cloned_vec(context) {
            match queue_tx {
                QueueTx::SignedTx(signed_tx) => {
                    // validate nonce
                    if signed_tx.nonce() != executor.get_nonce(signed_tx.sender) {
                        // if invalid nonce, do not process and remove from block
                        evicted_transactions.push(hex::encode(hash))
                    }

                    let (
                        TxResponse {
                            exit_reason,
                            logs,
                            used_gas,
                            ..
                        },
                        receipt,
                    ) = executor.exec(&signed_tx);
                    debug!(
                        "receipt : {:#?} for signed_tx : {:#x}",
                        receipt,
                        signed_tx.transaction.hash()
                    );

                    all_transactions.push(signed_tx.clone());
                    gas_used += used_gas;
                    EVMHandler::logs_bloom(logs, &mut logs_bloom);
                    receipts_v3.push(receipt);
                }
                QueueTx::BridgeTx(BridgeTx::EvmIn(BalanceUpdate { address, amount })) => {
                    debug!(
                        "[finalize_block] EvmIn for address {:x?}, amount: {}, context {}",
                        address, amount, context
                    );
                    if let Err(e) = executor.add_balance(address, amount) {
                        debug!("[finalize_block] EvmIn failed with {e}");
                        evicted_transactions.push(hex::encode(hash));
                    }
                }
                QueueTx::BridgeTx(BridgeTx::EvmOut(BalanceUpdate { address, amount })) => {
                    debug!(
                        "[finalize_block] EvmOut for address {}, amount: {}",
                        address, amount
                    );

                    if let Err(e) = executor.sub_balance(address, amount) {
                        debug!("[finalize_block] EvmOut failed with {e}");
                        evicted_transactions.push(hex::encode(hash));
                    }
                }
            }

            executor.commit();
        }

        if update_state {
            self.evm.tx_queues.remove(context);
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
                receipts_root: ReceiptHandler::get_receipts_root(&receipts_v3),
                logs_bloom,
                difficulty: U256::from(difficulty),
                number: current_block_number,
                gas_limit: U256::from(MAX_GAS_PER_BLOCK),
                gas_used: U256::from(gas_used),
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
            // calculate base fee
            let base_fee = self.block.calculate_base_fee(parent_hash);

            self.block.connect_block(block.clone(), base_fee);
            self.logs
                .generate_logs_from_receipts(&receipts, block.header.number);
            self.receipt.put_receipts(receipts);
        }

        Ok((
            *block.header.hash().as_fixed_bytes(),
            evicted_transactions,
            gas_used,
        ))
    }
}
