use crate::block::BlockHandler;
use crate::evm::{get_vicinity, EVMHandler};
use crate::executor::{AinExecutor, TxResponse};
use crate::receipt::ReceiptHandler;
use crate::storage::Storage;
use crate::traits::Executor;

use ethereum::{Block, BlockAny, PartialHeader, ReceiptV3, TransactionV2};
use ethereum_types::{Bloom, H160, U256};
use evm::backend::MemoryBackend;
use std::error::Error;
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

pub struct Handlers {
    pub evm: EVMHandler,
    pub block: BlockHandler,
    pub storage: Arc<Storage>,
    pub receipt: ReceiptHandler,
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
        let mut all_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut failed_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut receipts_v3: Vec<ReceiptV3> = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut gas_used = 0u64;
        let mut logs_bloom: Bloom = Default::default();

        let vicinity = get_vicinity(None, None);
        let state = self.evm.tx_queues.state(context).expect("Wrong context");
        let backend = MemoryBackend::new(&vicinity, state);
        let mut executor = AinExecutor::new(backend);

        for signed_tx in self.evm.tx_queues.drain_all(context) {
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
        }

        self.evm.tx_queues.remove(context);

        let (parent_hash, number) = self.block.get_latest_block_hash_and_number();

        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary: miner_address.unwrap_or_default(),
                state_root: Default::default(),
                receipts_root: ReceiptHandler::get_receipts_root(&receipts_v3),
                logs_bloom,
                difficulty: U256::from(difficulty),
                number,
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
            let mut state = self.evm.state.write().unwrap();
            *state = executor.backend().state().clone();

            self.block.connect_block(block.clone());
            self.receipt.put_receipts(receipts);
        }

        Ok((block, failed_transactions))
    }
}
