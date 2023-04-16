use crate::block::BlockHandler;
use crate::evm::{get_vicinity, EVMHandler};
use crate::executor::AinExecutor;
use crate::storage::Storage;
use crate::traits::Executor;
use ethereum::{Block, BlockAny, PartialHeader, TransactionV2};
use evm::backend::MemoryBackend;
use primitive_types::{H160, H256, U256};
use std::error::Error;
use std::time::{SystemTime, UNIX_EPOCH};

pub struct Handlers {
    pub evm: EVMHandler,
    pub block: BlockHandler,
    pub storage: Storage,
}

impl Default for Handlers {
    fn default() -> Self {
        Self::new()
    }
}

impl Handlers {
    pub fn new() -> Self {
        Self {
            evm: EVMHandler::new(),
            block: BlockHandler::new(),
            storage: Storage::new(),
        }
    }

    pub fn finalize_block(
        &self,
        context: u64,
        update_state: bool,
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
                successful_transactions.push(signed_tx.transaction);
            } else {
                failed_transactions.push(signed_tx.transaction)
            }
        }

        let mut all_transactions = successful_transactions.clone();
        all_transactions.extend(failed_transactions.clone());

        self.evm.tx_queues.remove(context);

        let (parent_hash, number) = {
            self.storage
                .get_latest_block().map(|first_block| (first_block.header.hash(), first_block.header.number + 1))
                .unwrap_or((H256::default(), U256::zero()))
        };

        let block = Block::new(
            PartialHeader {
                parent_hash,
                beneficiary: Default::default(),
                state_root: Default::default(),
                receipts_root: Default::default(),
                logs_bloom: Default::default(),
                difficulty: Default::default(),
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

        self.block.connect_block(block.clone());

        if update_state {
            let mut state = self.evm.state.write().unwrap();
            *state = executor.backend().state().clone();

            self.storage.put_latest_block(block.clone());
            self.storage.put_block(block.clone());
        }

        Ok((block, failed_transactions))
    }
}
