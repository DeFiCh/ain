use std::collections::BTreeMap;
use std::error::Error;
use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;
use std::sync::{Arc, RwLock};
use evm::executor::stack::{MemoryStackState, StackExecutor, StackSubstateMetadata};
use primitive_types::{H160, H256, U256};
use crate::traits::PersistentState;
use ain_evm::{executor::AinExecutor, traits::Executor, transaction::SignedTx};
use anyhow::anyhow;
use ethereum::{AccessList, Block, PartialHeader, TransactionV2};
use evm::{
    backend::{MemoryBackend, MemoryVicinity},
    ExitReason, Config,
};
use hex::FromHex;
use evm::backend::MemoryAccount;
use crate::tx_queue::TransactionQueueMap;

pub static CONFIG: Config = Config::london();
pub static GAS_LIMIT: u64 = u64::MAX;
pub static EVM_STATE_PATH: &str = "evm_state.bin";

pub type EVMState = BTreeMap<H160, MemoryAccount>;

#[derive(Clone, Debug)]
pub struct EVMHandler {
    pub state: Arc<RwLock<EVMState>>,
    pub tx_queues: Arc<TransactionQueueMap>,
}

impl PersistentState for EVMState {
    fn save_to_disk(&self, path: &str) -> Result<(), String> {
        let serialized_state = bincode::serialize(self).map_err(|e| e.to_string())?;
        let mut file = File::create(path).map_err(|e| e.to_string())?;
        file.write_all(&serialized_state).map_err(|e| e.to_string())
    }

    fn load_from_disk(path: &str) -> Result<Self, String> {
        if Path::new(path).exists() {
            let mut file = File::open(path).map_err(|e| e.to_string())?;
            let mut data = Vec::new();
            file.read_to_end(&mut data).map_err(|e| e.to_string())?;
            let new_state: BTreeMap<H160, MemoryAccount> =
                bincode::deserialize(&data).map_err(|e| e.to_string())?;
            Ok(new_state)
        } else {
            Ok(Self::new())
        }
    }
}


impl EVMHandler {
    pub fn new() -> Self {
        Self {
            state: Arc::new(RwLock::new(EVMState::new())),
            tx_queues: Arc::new(TransactionQueueMap::new()),
        }
    }

    pub fn flush(&self) {
        self.state.write().unwrap().save_to_disk(EVM_STATE_PATH).unwrap()
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
        let tx_response =
            AinExecutor::new(backend).call(caller, to, value, data, gas_limit, access_list, false);
        (tx_response.exit_reason, tx_response.data)
    }

    // TODO wrap in EVM transaction and dryrun with evm_call
    pub fn add_balance(&self, address: &str, value: U256) -> Result<(), Box<dyn Error>> {
        let to = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.entry(to).or_default();
        account.balance = account.balance + value;
        Ok(())
    }

    pub fn sub_balance(&self, address: &str, value: U256) -> Result<(), Box<dyn Error>> {
        let address = address.parse()?;
        let mut state = self.state.write().unwrap();
        let mut account = state.get_mut(&address).unwrap();
        if account.balance > value {
            account.balance = account.balance - value;
        }
        Ok(())
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
    ) -> Result<(Block<TransactionV2>, Vec<TransactionV2>), Box<dyn Error>> {
        let mut tx_hashes = Vec::with_capacity(self.tx_queues.len(context));
        let mut failed_tx_hashes = Vec::with_capacity(self.tx_queues.len(context));
        let vicinity = get_vicinity(None, None);
        let state = self.state.read().unwrap().clone();
        let backend = MemoryBackend::new(&vicinity, state);
        let mut executor = AinExecutor::new(backend);

        for signed_tx in self.tx_queues.drain_all(context) {
            let tx_response = executor.exec(&signed_tx);
            println!("tx_response : {:#?}", tx_response);

            if tx_response.exit_reason.is_succeed() {
                // responses.push()
                tx_hashes.push(signed_tx.transaction);
            } else {
                failed_tx_hashes.push(signed_tx.transaction)
            }
        }

        if update_state {
            let mut state = self.state.write().unwrap();
            *state = executor.backend().state().clone();
        }

        let block = Block::new(
            PartialHeader {
                parent_hash: Default::default(),
                beneficiary: Default::default(),
                state_root: Default::default(),
                receipts_root: Default::default(),
                logs_bloom: Default::default(),
                difficulty: Default::default(),
                number: Default::default(),
                gas_limit: Default::default(),
                gas_used: Default::default(),
                timestamp: Default::default(),
                extra_data: Default::default(),
                mix_hash: Default::default(),
                nonce: Default::default(),
            },
            tx_hashes,
            Vec::new(),
        );
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


#[cfg(test)]
mod tests {
    use super::*;
    use primitive_types::{H256, U256};

    fn create_account(
        nonce: U256,
        balance: U256,
        code: Vec<u8>,
        storage: BTreeMap<H256, H256>,
    ) -> MemoryAccount {
        MemoryAccount {
            nonce,
            balance,
            code,
            storage,
        }
    }

    #[test]
    fn test_load_non_existent_file() {
        let state = EVMState::load_from_disk("non_existent_file.bin").unwrap();
        assert_eq!(state, EVMState::default());
    }

    #[test]
    fn test_empty_file() {
        let state = BTreeMap::new();
        let path = "empty_test.bin";

        // Save to an empty file
        state.save_to_disk(path).unwrap();

        let new_state = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, new_state);
    }

    #[test]
    fn test_invalid_file_format() {
        let invalid_data = b"invalid_data";
        let path = "invalid_file_format.bin";

        // Write invalid data to a file
        let mut file = File::create(path).unwrap();
        file.write_all(invalid_data).unwrap();

        let state = EVMState::load_from_disk(path);

        assert!(state.is_err());
    }

    #[test]
    fn test_save_and_load_empty_backend() {
        let path = "test_empty_backend.bin";
        let state = BTreeMap::new();

        state.save_to_disk(path).unwrap();

        let loaded_backend = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, loaded_backend);
    }

    #[test]
    fn test_save_and_load_single_account() {
        let path = "test_single_account.bin";
        let mut state = BTreeMap::new();

        let account = create_account(
            U256::from(1),
            U256::from(1000),
            vec![1, 2, 3],
            BTreeMap::new(),
        );
        let address = H160::from_low_u64_be(1);
        state.insert(address, account);

        state.save_to_disk(path).unwrap();

        let loaded_backend = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, loaded_backend);
    }

    #[test]
    fn test_save_and_load_multiple_accounts() {
        let path = "test_multiple_accounts.bin";
        let mut state = BTreeMap::new();

        let account1 = create_account(
            U256::from(1),
            U256::from(1000),
            vec![1, 2, 3],
            BTreeMap::new(),
        );
        let address1 = H160::from_low_u64_be(1);
        state.insert(address1, account1);

        let account2 = create_account(
            U256::from(2),
            U256::from(2000),
            vec![4, 5, 6],
            BTreeMap::new(),
        );
        let address2 = H160::from_low_u64_be(2);
        state.insert(address2, account2);

        state.save_to_disk(path).unwrap();

        let loaded_backend = EVMState::load_from_disk(path).unwrap();

        assert_eq!(state, loaded_backend);
    }
}

mod tests {
    use ain_evm::transaction::SignedTx;
    use evm::backend::MemoryAccount;
    use primitive_types::{H160, H256, U256};

    use super::EVMHandler;
    use rlp::Encodable;
    #[test]
    fn test_finalize_block_and_update_test() {
        let handler = EVMHandler::new();
        handler
            .add_balance(
                "0x6745f998a96050bb9b0449e6bd4358138a519679",
                U256::from_str_radix("100000000000000000000", 10).unwrap(),
            )
            .unwrap();
        let context = handler.get_context();

        let tx1: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx1.clone());

        handler
            .add_balance(
                "0xc0cd829081485e70348975d325fe5275140277bd",
                U256::from_str_radix("100000000000000000000", 10).unwrap(),
            )
            .unwrap();
        let tx2: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a01465e2d999c34b22bf4b8b5c9439918e46341f4f0da1b00a6b0479c541161d4aa074abe79c51bf57086e1e84b57ee483cbb2ecf30e8222bc0472436fabfc57dda8".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx2.clone());

        let tx3: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a070b21a24cec13c0569099ee2f8221268103fd609646b73f7c9e85efeb7af5c8ea03d5de75bc12ce28a80f7c0401df6021cc82a334cb1c802c8b9d46223c5c8eb40".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx3.clone());

        assert_eq!(handler.tx_queues.len(context), 3);
        assert_eq!(handler.tx_queues.len(handler.get_context()), 0);

        let (block, failed_txs) = handler.finalize_block(context, true).unwrap();
        assert_eq!(
            block.transactions,
            vec![tx1, tx2]
                .into_iter()
                .map(|t| t.transaction)
                .collect::<Vec<_>>()
        );
        assert_eq!(
            failed_txs,
            vec![tx3]
                .into_iter()
                .map(|t| t.transaction)
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
                    &"0x6745f998a96050bb9b0449e6bd4358138a519679"
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
                    &"0xc0cd829081485e70348975d325fe5275140277bd"
                        .parse()
                        .unwrap()
                )
                .unwrap()
                .balance,
            U256::from_str_radix("0", 10).unwrap()
        );
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

        let tx1: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
        handler.tx_queues.add_signed_tx(context, tx1.clone());

        let old_state = handler.state.read().unwrap();
        let _ = handler.finalize_block(context, false).unwrap();

        let new_state = handler.state.read().unwrap();
        assert_eq!(*new_state, *old_state);
    }
}
