use ain_evm::{block::Block, executor::AinExecutor, traits::Executor, transaction::SignedTx};
use anyhow::anyhow;
use ethereum::{AccessList, TransactionV2};
use evm::{
    backend::{MemoryBackend, MemoryVicinity},
    ExitReason,
};
use hex::FromHex;
use primitive_types::{H160, H256, U256};
use std::error::Error;
use std::sync::{Arc, RwLock};

use crate::tx_queue::TransactionQueueMap;
use crate::EVMState;

#[derive(Debug)]
struct ErrorStr(String);

impl std::fmt::Display for ErrorStr {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "Error: {}", self.0)
    }
}

impl Error for ErrorStr {}

#[derive(Clone, Debug)]
pub struct EVMHandler {
    pub state: Arc<RwLock<EVMState>>,
    pub tx_queues: Arc<TransactionQueueMap>,
}

impl EVMHandler {
    pub fn new() -> Self {
        Self {
            state: Arc::new(RwLock::new(EVMState::new())),
            tx_queues: Arc::new(TransactionQueueMap::new()),
        }
    }

    pub fn call(
        &self,
        caller: Option<H160>,
        to: Option<H160>,
        value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
    ) -> (ExitReason, Vec<u8>) {
        // TODO Add actual gas, chain_id, block_number from header
        let vicinity = get_vicinity(caller, None);

        let state = self.state.read().unwrap().clone();
        let backend = MemoryBackend::new(&vicinity, state);
        AinExecutor::new(backend).call(caller, to, value, data, gas_limit, access_list, false)
    }

    // TODO wrap in EVM transaction and dryrun with evm_call
    pub fn add_balance(&self, context: u64, address: &str, value: U256) -> Result<(), Box<dyn Error>> {
        let to = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.entry(to).or_default();
        account.balance = account.balance + value;
        Ok(())
    }

    pub fn sub_balance(&self, context: u64, address: &str, value: U256) -> Result<(), Box<dyn Error>> {
        let address = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.get_mut(&address).unwrap();
        if account.balance > value {
            account.balance = account.balance - value;
            return Ok(());
        }
        Err(Box::new(ErrorStr("Sub balance failed".into())))
    }

    pub fn validate_raw_tx(&self, tx: &str) -> Result<SignedTx, Box<dyn Error>> {
        let buffer = <Vec<u8>>::from_hex(tx)?;
        let tx: TransactionV2 = ethereum::EnvelopedDecodable::decode(&buffer)
            .map_err(|_| anyhow!("Error: decoding raw tx to TransactionV2"))?;

        // TODO Validate gas limit and chain_id

        let signed_tx: SignedTx = tx.try_into()?;

        // TODO validate account nonce and balance to pay gas
        // let account = self.get_account(&signed_tx.sender);
        // if account.nonce >= signed_tx.nonce() {
        //     return Err(anyhow!("Invalid nonce").into());
        // }
        // if account.balance < MIN_GAS {
        //     return Err(anyhow!("Insufficiant balance to pay fees").into());
        // }

        match self.call(
            Some(signed_tx.sender),
            signed_tx.to(),
            signed_tx.value(),
            signed_tx.data(),
            signed_tx.gas_limit().as_u64(),
            signed_tx.access_list(),
        ) {
            (exit_reason, _) if exit_reason.is_succeed() => Ok(signed_tx),
            _ => Err(anyhow!("Error calling EVM").into()),
        }
    }

    pub fn get_context(&self) -> u64 {
        self.tx_queues.get_context()
    }

    pub fn discard_context(&self, context: u64) {
        self.tx_queues.clear(context)
    }

    pub fn queue_tx(&self, context: u64, raw_tx: &str) -> Result<(), Box<dyn Error>> {
        let signed_tx = self.validate_raw_tx(raw_tx)?;
        self.tx_queues.add_signed_tx(context, signed_tx);
        Ok(())
    }

    pub fn finalize_block(
        &self,
        context: u64,
        update_state: bool,
    ) -> Result<(Block, Vec<H256>), Box<dyn Error>> {
        let mut tx_hashes = Vec::with_capacity(self.tx_queues.len(context));
        let mut failed_tx_hashes = Vec::with_capacity(self.tx_queues.len(context));

        let vicinity = get_vicinity(None, None);
        let state = self.state.read().unwrap().clone();
        let backend = MemoryBackend::new(&vicinity, state);
        let mut executor = AinExecutor::new(backend);

        for signed_tx in self.tx_queues.drain_all(context) {
            let (exit_reason, _) = executor.exec(&signed_tx);

            if exit_reason.is_succeed() {
                tx_hashes.push(signed_tx.transaction.hash());
            } else {
                failed_tx_hashes.push(signed_tx.transaction.hash())
            }
        }

        if update_state {
            let mut state = self.state.write().unwrap();
            *state = executor.backend().state().clone();
        }

        let block = Block::new(tx_hashes);
        Ok((block, failed_tx_hashes))
    }
}

// TBD refine what vicinity we need. gas_price and origin only ?
fn get_vicinity(origin: Option<H160>, gas_price: Option<U256>) -> MemoryVicinity {
    MemoryVicinity {
        gas_price: gas_price.unwrap_or_else(|| U256::MAX),
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

mod tests {
    use ain_evm::transaction::SignedTx;
    use evm::backend::MemoryAccount;
    use primitive_types::{H160, H256, U256};

    use super::EVMHandler;

    #[test]
    fn test_finalize_block_and_update_test() {
        let handler = EVMHandler::new();
        handler
            .add_balance(
                "0x4a1080c5533cb89edc4b65013f08f78868e382de",
                U256::from_str_radix("100000000000000000000", 10).unwrap(),
            )
            .unwrap();
        let context = handler.get_context();

        let tx1: SignedTx = "f86b0285174876e8006494a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx1.clone());

        handler
            .add_balance(
                "0x47e0d33aeaaaba1a0367ee9adbf87c828f05ce3d",
                U256::from_str_radix("100000000000000000000", 10).unwrap(),
            )
            .unwrap();
        let tx2: SignedTx = "f86b0285174876e8006494a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a01465e2d999c34b22bf4b8b5c9439918e46341f4f0da1b00a6b0479c541161d4aa074abe79c51bf57086e1e84b57ee483cbb2ecf30e8222bc0472436fabfc57dda8".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx2.clone());

        let tx3: SignedTx = "f86b0285174876e8006494a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a070b21a24cec13c0569099ee2f8221268103fd609646b73f7c9e85efeb7af5c8ea03d5de75bc12ce28a80f7c0401df6021cc82a334cb1c802c8b9d46223c5c8eb40".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx3.clone());

        assert_eq!(handler.tx_queues.len(context), 3);
        assert_eq!(handler.tx_queues.len(handler.get_context()), 0);

        let (block, failed_txs) = handler.finalize_block(context, true).unwrap();
        assert_eq!(
            block.txs,
            vec![tx1, tx2]
                .into_iter()
                .map(|t| t.transaction.hash())
                .collect::<Vec<_>>()
        );
        assert_eq!(
            failed_txs,
            vec![tx3]
                .into_iter()
                .map(|t| t.transaction.hash())
                .collect::<Vec<_>>()
        );

        let state = handler.state.read().unwrap();
        assert_eq!(
            state
                .get(
                    &"0xa8f7c4c78c36e54c3950ad58dad24ca5e0191b29"
                        .parse()
                        .unwrap()
                )
                .unwrap()
                .balance,
            U256::from_str_radix("200000000000000000000", 10).unwrap()
        );
        assert_eq!(
            state
                .get(
                    &"0x4a1080c5533cb89edc4b65013f08f78868e382de"
                        .parse()
                        .unwrap()
                )
                .unwrap()
                .balance,
            U256::from_str_radix("0", 10).unwrap()
        );
        assert_eq!(
            state
                .get(
                    &"0x47e0d33aeaaaba1a0367ee9adbf87c828f05ce3d"
                        .parse()
                        .unwrap()
                )
                .unwrap()
                .balance,
            U256::from_str_radix("0", 10).unwrap()
        )
    }

    #[test]
    fn test_finalize_block_and_do_not_update_test() {
        let handler = EVMHandler::new();
        handler
            .add_balance(
                "0x4a1080c5533cb89edc4b65013f08f78868e382de",
                U256::from_str_radix("100000000000000000000", 10).unwrap(),
            )
            .unwrap();
        let context = handler.get_context();

        let tx1: SignedTx = "f86b0285174876e8006494a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx1.clone());

        let old_state = handler.state.read().unwrap();
        let _ = handler.finalize_block(context, false).unwrap();

        let new_state = handler.state.read().unwrap();
        assert_eq!(*new_state, *old_state);
    }
}
