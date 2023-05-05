use crate::backend::{EVMBackend, Vicinity};
use crate::block::BlockHandler;
use crate::evm::EVMHandler;
use crate::executor::{AinExecutor, TxResponse};
use crate::receipt::ReceiptHandler;
use crate::storage::traits::BlockStorage;
use crate::storage::Storage;
use crate::traits::Executor;
use crate::tx_queue::{BalanceUpdate, QueueTx};

use anyhow::anyhow;
use ethereum::{Block, BlockAny, PartialHeader, ReceiptV3, TransactionV2};
use ethereum_types::{Bloom, H160, U256};
use log::debug;
use primitive_types::H256;
use std::error::Error;
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

const GENESIS_STATE_ROOT: &str =
    "0xbc36789e7a1e281436464229828f817d6612f7b477d66591ff96a9e064bcc98a";

pub struct Handlers {
    pub evm: EVMHandler,
    pub block: BlockHandler,
    pub receipt: ReceiptHandler,
    pub storage: Arc<Storage>,
}

impl Default for Handlers {
    fn default() -> Self {
        Self::new()
    }
}

impl Handlers {
    pub fn new() -> Self {
        let storage = Arc::new(Storage::new());
        Self {
            evm: EVMHandler::new(Arc::clone(&storage)),
            block: BlockHandler::new(Arc::clone(&storage)),
            receipt: ReceiptHandler::new(Arc::clone(&storage)),
            storage,
        }
    }

    pub fn finalize_block(
        &self,
        context: u64,
        update_state: bool,
        difficulty: u32,
        miner_address: Option<H160>,
    ) -> Result<(BlockAny, Vec<TransactionV2>), Box<dyn Error>> {
        println!("*******************************************");
        println!("-------------------------------------------");
        println!("--------- START FINALIZE_BLOCK-------------");
        println!("-------------------------------------------");
        debug!("context: {}, update_state: {}", context, update_state);
        println!("context: {}, update_state: {}", context, update_state);

        let mut all_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut failed_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut receipts_v3: Vec<ReceiptV3> = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut gas_used = 0u64;
        let mut logs_bloom: Bloom = Default::default();

        let state_root = self
            .storage
            .get_latest_block()
            .map(|block| block.header.state_root)
            .unwrap_or(GENESIS_STATE_ROOT.parse().unwrap());
        // .unwrap_or_default();

        let vicinity = Vicinity {
            gas_price: U256::from(1000000000),
            block_number: U256::from(3),
            origin: H160::default(),
            logs: Vec::new(),
        };

        println!("state_root : {:#?}", state_root);

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.evm.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )?;

        let mut test_address: H160 = H160::zero();
        {
            let mut executor = AinExecutor::new(&mut backend);

            for queue_tx in self.evm.tx_queues.drain_all(context) {
                println!("queue_tx : {:#?}", queue_tx);
                match queue_tx {
                    QueueTx::SignedTx(signed_tx) => {
                        let TxResponse {
                            exit_reason,
                            logs,
                            used_gas,
                            receipt,
                            ..
                        } = executor.exec(&signed_tx);
                        if exit_reason.is_succeed() {
                            all_transactions.push(signed_tx);
                        } else {
                            failed_transactions.push(signed_tx.transaction.clone());
                            all_transactions.push(signed_tx);
                        }

                        gas_used += used_gas;
                        EVMHandler::logs_bloom(logs, &mut logs_bloom);
                        receipts_v3.push(receipt);
                        println!("commit in signedtx");
                    }
                    QueueTx::AddBalance(BalanceUpdate { address, amount }) => {
                        // if !update_state {
                        //     continue;
                        // }
                        debug!(
                            "EvmIn for address {:x?}, amount: {}, context {}",
                            address, amount, context
                        );
                        println!(
                            "EvmIn for address {:x?}, amount: {}, context: {}",
                            address, amount, context
                        );
                        let account = executor.backend.get_account(address);
                        println!("account before : {:#?}", account);
                        let r = executor.add_balance(address, amount); // Need to return this fail tx somehow
                        println!("result of add_balance : {:#?}", r);
                        let account_after = executor.backend.get_account(address);
                        println!("account_after : {:#?}", account_after);
                        // if update_state {
                        executor.backend.commit();
                        // }

                        println!("- --- trying restore old backend ----");
                        println!("store : {:#?} ", bincode::serialize(&*self.evm.trie_store));
                        let vicinity = Vicinity {
                            gas_price: U256::from(0),
                            block_number: U256::from(3),
                            origin: H160::default(),
                            logs: Vec::new(),
                        };
                        let backend = EVMBackend::from_root(
                            state_root,
                            Arc::clone(&self.evm.trie_store),
                            Arc::clone(&self.storage),
                            vicinity,
                        )
                        .map_err(|e| anyhow!(" ??? Could not restore backend : {}", e))?;
                        let account = backend.get_account(address);
                        println!("account after restore old : {:#?}", account);

                        test_address = address;
                    }
                    QueueTx::SubBalance(BalanceUpdate { address, amount }) => {
                        debug!("EvmOut for address {}, amount: {}", address, amount);
                        executor.sub_balance(address, amount).unwrap(); // Need to return this fail tx somehow
                                                                        // executor.backend.commit();
                    }
                }
            }
        }

        self.evm.tx_queues.remove(context);

        let (parent_hash, parent_number) = self.block.get_latest_block_hash_and_number();

        // backend.commit();
        let mut block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary: miner_address.unwrap_or_default(),
                state_root: Default::default(),
                receipts_root: ReceiptHandler::get_receipts_root(&receipts_v3),
                logs_bloom,
                difficulty: U256::from(difficulty),
                number: parent_number + 1,
                gas_limit: U256::from(30000000),
                gas_used: U256::from(gas_used),
                timestamp: SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .unwrap()
                    .as_millis() as u64,
                extra_data: Default::default(),
                mix_hash: Default::default(),
                nonce: Default::default(),
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
            let new_state_root = backend.commit();
            debug!("new_state_root : {:#?}", new_state_root);
            println!("new_state_root : {:#?}", new_state_root);
            block.header.state_root = new_state_root;
            self.block.connect_block(block.clone());
            self.receipt.put_receipts(receipts);

            let vicinity = Vicinity {
                gas_price: U256::from(0),
                block_number: U256::from(3),
                origin: H160::default(),
                logs: Vec::new(),
            };
            let mut backend = EVMBackend::from_root(
                state_root,
                Arc::clone(&self.evm.trie_store),
                Arc::clone(&self.storage),
                vicinity,
            )
            .map_err(|e| anyhow!(" ??? Could not restore backend : {}", e))?;
            let account = backend.get_account(test_address);
            println!(
                "account after restore old - -block.header.state_root : {:#?}",
                account
            );
        }

        println!("-------------------------------------------");
        println!("----------- END FINALIZE_BLOCK-------------");
        println!("-------------------------------------------");
        Ok((block, failed_transactions))
    }
}
