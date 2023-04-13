use crate::block::BlockHandler;
use crate::evm::EVMHandler;
use crate::executor::AinExecutor;
use crate::storage::Storage;
use crate::traits::Executor;
use ethereum::{Block, BlockAny, PartialHeader, TransactionV2};
use evm::backend::{MemoryBackend, MemoryVicinity};
use primitive_types::{H160, H256, U256};
use std::error::Error;
use std::time::{SystemTime, UNIX_EPOCH};

pub struct Handlers {
    pub evm: EVMHandler,
    pub block: BlockHandler,
    pub storage: Storage,
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
        miner_address: Option<H160>,
    ) -> Result<(BlockAny, Vec<TransactionV2>), Box<dyn Error>> {
        let mut tx_hashes = Vec::with_capacity(self.evm.tx_queues.len(context));
        let mut failed_tx_hashes = Vec::with_capacity(self.evm.tx_queues.len(context));
        let vicinity = get_vicinity(None, None);
        let state = self.evm.tx_queues.state(context).expect("Wrong context");
        let backend = MemoryBackend::new(&vicinity, state);
        let mut executor = AinExecutor::new(backend);

        for signed_tx in self.evm.tx_queues.drain_all(context) {
            let tx_response = executor.exec(&signed_tx);
            if tx_response.exit_reason.is_succeed() {
                tx_hashes.push(signed_tx.transaction);
            } else {
                failed_tx_hashes.push(signed_tx.transaction)
            }
        }

        self.evm.tx_queues.remove(context);

        let (parent_hash, number) = {
            self.storage
                .get_latest_block()
                .and_then(|first_block| {
                    Some((first_block.header.hash(), first_block.header.number + 1))
                })
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
                number: U256::from(number),
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
            tx_hashes,
            Vec::new(),
        );

        self.block.connect_block(block.clone());

        if update_state {
            let mut state = self.evm.state.write().unwrap();
            *state = executor.backend().state().clone();

            self.storage.put_latest_block(block.clone());
            self.storage.put_block(block.clone());
        }

        Ok((block, failed_tx_hashes))
    }
}

fn get_vicinity(origin: Option<H160>, gas_price: Option<U256>) -> MemoryVicinity {
    MemoryVicinity {
        gas_price: gas_price.unwrap_or(U256::MAX),
        origin: origin.unwrap_or_default(),
        block_hashes: Vec::new(),
        block_number: Default::default(),
        block_coinbase: Default::default(),
        block_timestamp: Default::default(),
        block_difficulty: Default::default(),
        block_gas_limit: U256::MAX,
        chain_id: U256::one(),
        block_base_fee_per_gas: U256::MAX,
    }
}
