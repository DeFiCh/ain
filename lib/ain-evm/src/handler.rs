use crate::block::BlockHandler;
use crate::evm::{get_vicinity, EVMHandler};
use crate::executor::AinExecutor;
use crate::receipt::ReceiptHandler;
use crate::storage::Storage;
use crate::traits::Executor;

use ethereum::{Block, BlockAny, PartialHeader, TransactionV2};
use evm::backend::MemoryBackend;
use primitive_types::{H160, U256};
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
            evm: EVMHandler::new(),
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
        _miner_address: Option<H160>,
    ) -> Result<(BlockAny, Vec<TransactionV2>), Box<dyn Error>> {
        let mut successful_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut failed_transactions = Vec::with_capacity(self.evm.tx_queues.len(context));
        let vicinity = get_vicinity(None, None);
        let state = self.evm.tx_queues.state(context).expect("Wrong context");
        let backend = MemoryBackend::new(&vicinity, state);
        let mut executor = AinExecutor::new(backend);

        for signed_tx in self.evm.tx_queues.drain_all(context) {
            let tx_response = executor.exec(&signed_tx);
            if tx_response.exit_reason.is_succeed() {
                successful_transactions.push(signed_tx);
            } else {
                failed_transactions.push(signed_tx)
            }
        }

        let mut all_transactions = successful_transactions
            .clone()
            .into_iter()
            .map(|tx| tx.transaction)
            .collect::<Vec<TransactionV2>>();
        all_transactions.extend(
            failed_transactions
                .clone()
                .into_iter()
                .map(|tx| tx.transaction)
                .collect::<Vec<TransactionV2>>(),
        );

        self.evm.tx_queues.remove(context);

        let (parent_hash, number) = self.block.get_latest_block_and_number();

        let mut block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary: Default::default(),
                state_root: Default::default(),
                receipts_root: Default::default(),
                logs_bloom: Default::default(),
                difficulty: U256::from(difficulty),
                number,
                gas_limit: Default::default(),
                gas_used: Default::default(),
                timestamp: SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .unwrap()
                    .as_millis() as u64,
                extra_data: Default::default(),
                mix_hash: Default::default(),
                nonce: Default::default(),
            },
            all_transactions,
            Vec::new(),
        );

        let receipts = self.receipt.generate_receipts(
            successful_transactions,
            failed_transactions.clone(),
            block.header.hash(),
            block.header.number,
        );
        block.header.receipts_root = self.receipt.get_receipt_root(&receipts);

        if update_state {
            let mut state = self.evm.state.write().unwrap();
            *state = executor.backend().state().clone();

            self.block.connect_block(block.clone());
            self.receipt.put_receipts(receipts);
        }

        Ok((
            block,
            failed_transactions
                .into_iter()
                .map(|tx| tx.transaction)
                .collect(),
        ))
    }
}
