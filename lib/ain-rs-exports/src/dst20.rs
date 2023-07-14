use ain_evm::runtime::RUNTIME;
use ain_evm::transaction::system::{DeployContractData, SystemTx};
use ain_evm::tx_queue::QueueTx;
use ethereum::{EIP1559TransactionMessage, TransactionAction, TransactionV2};
use ethers::abi::{Abi, Token};
use ethers::types::Bytes;
use ethers::utils::keccak256;
use libsecp256k1::SecretKey;
use log::debug;
use primitive_types::{H160, H256, U256};
use std::error::Error;
use std::str::FromStr;
use std::sync::RwLock;

lazy_static::lazy_static! {
    // Global runtime exposed by the library
    pub static ref SYSTEM_NONCE: SystemNonceHandler = SystemNonceHandler::new(H160::from_str(PUBLIC_KEY).expect("Unable to construct address")).expect("Unable to create SystemNonceHandler");
}

const PUBLIC_KEY: &str = "0xeB4B222C3dE281d40F5EBe8B273106bFcC1C1b94";
const PRIVATE_KEY: &str = "074016d5336e9bb4f204ea5bb536ba5c222ae836b92881a8de8de44e1dfea3a2";

pub struct SystemNonceHandler {
    pub nonce: RwLock<U256>,
}

impl SystemNonceHandler {
    // This is required for when we can have multiple system transactions in the same block
    fn new(address: H160) -> Result<Self, Box<dyn Error>> {
        let block_number = match RUNTIME.handlers.block.get_latest_block_hash_and_number() {
            None => return Err("Unable to get block".into()),
            Some((_, number)) => number,
        };

        let nonce = RUNTIME
            .handlers
            .evm
            .get_nonce(address, block_number)
            .map_err(|e| Box::new(e) as Box<dyn Error>)?;

        Ok(Self {
            nonce: RwLock::new(nonce),
        })
    }

    fn get_and_increment_nonce(&self, apply_changes: bool) -> U256 {
        if !apply_changes {
            let nonce = self.nonce.read().unwrap();
            debug!("Not increasing nonce, nonce: {:?}", *nonce);

            *nonce
        } else {
            let mut nonce = self.nonce.write().unwrap();
            *nonce += U256::one();
            debug!("Increasing nonce, nonce: {:?}", *nonce - U256::one());

            *nonce - U256::one()
        }
    }
}

/// Generates bytecode and creates a system transaction in mempool
pub fn deploy_dst20(
    native_hash: [u8; 32],
    context: u64,
    apply_changes: bool,
    name: String,
    symbol: String,
) -> Result<(), Box<dyn Error>> {
    let system_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        name,
        symbol,
    }));

    RUNTIME
        .handlers
        .queue_tx(context, system_tx.into(), native_hash)?;

    Ok(())
}

fn sign_eip1559(m: EIP1559TransactionMessage) -> Result<TransactionV2, Box<dyn Error>> {
    let secret_key = SecretKey::parse_slice(&hex::decode(PRIVATE_KEY)?[..])?;
    let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])?;

    let (signature, recovery_id) = libsecp256k1::sign(&signing_message, &secret_key);
    let rs = signature.serialize();
    let r = H256::from_slice(&rs[0..32]);
    let s = H256::from_slice(&rs[32..64]);

    Ok(TransactionV2::EIP1559(ethereum::EIP1559Transaction {
        chain_id: m.chain_id,
        nonce: m.nonce,
        max_priority_fee_per_gas: m.max_priority_fee_per_gas,
        max_fee_per_gas: m.max_fee_per_gas,
        gas_limit: m.gas_limit,
        action: m.action,
        value: m.value,
        input: m.input.clone(),
        access_list: m.access_list,
        odd_y_parity: recovery_id.serialize() != 0,
        r,
        s,
    }))
}
