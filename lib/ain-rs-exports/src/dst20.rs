use ain_evm::evm::{EthCallArgs, MAX_GAS_PER_BLOCK};
use ain_evm::executor::TxResponse;
use ain_evm::runtime::RUNTIME;
use ain_evm::transaction::SignedTx;
use ethereum::{EIP1559TransactionMessage, TransactionAction, TransactionV2};
use ethers::abi::{Abi, Token};
use ethers::types::Bytes;
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
    let bytecode_json: serde_json::Value =
        serde_json::from_str(include_str!("../dst20/output/bytecode.json"))
            .expect("Unable to read bytecode");
    let bytecode_raw = bytecode_json["object"]
        .as_str()
        .expect("Bytecode object not available");
    let bytecode: Bytes = Bytes::from(hex::decode(&bytecode_raw[2..]).expect("Decode failed"));
    let abi = Abi::load(include_str!("../dst20/output/abi.json").as_bytes())?;
    let address = H160::from_str(PUBLIC_KEY).expect("Unable to construct address");

    // generate TX data
    let input: Vec<u8> = abi.constructor().unwrap().encode_input(
        bytecode.to_vec(),
        &[Token::String(name), Token::String(symbol)],
    )?;

    // build transaction
    let chain_id = ain_cpp_imports::get_chain_id()?;

    let nonce = SYSTEM_NONCE.get_and_increment_nonce(apply_changes);

    let block_number = match RUNTIME.handlers.block.get_latest_block_hash_and_number() {
        None => return Err("Unable to get block".into()),
        Some((_, number)) => number,
    };

    let TxResponse { used_gas, .. } = RUNTIME.handlers.evm.call(EthCallArgs {
        caller: Some(address),
        to: None,
        value: Default::default(),
        data: input.as_slice(),
        gas_limit: MAX_GAS_PER_BLOCK.as_u64(),
        access_list: vec![],
        block_number,
    })?;

    let tx = EIP1559TransactionMessage {
        chain_id,
        nonce,
        max_priority_fee_per_gas: Default::default(), // we want this to be 0
        max_fee_per_gas: U256::max_value(), // do not have limit, this is constrained during mining
        gas_limit: U256::from(used_gas),
        action: TransactionAction::Create,
        value: Default::default(),
        input,
        access_list: vec![],
    };

    // sign tx
    let signed_tx: SignedTx = sign_eip1559(tx)?.try_into()?;
    debug!(
        "DST20 transaction created, tx hash: {:#?}, sender: {:#?}",
        signed_tx.transaction.hash(),
        signed_tx.sender
    );

    // add transaction to pool
    RUNTIME
        .handlers
        .queue_tx(context, signed_tx.into(), native_hash)
        .map_err(|e| Box::new(e) as Box<dyn Error>)
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
