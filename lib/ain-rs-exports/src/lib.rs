use ain_evm::transaction::{self, SignedTx};
use ain_grpc::{init_runtime, start_servers, stop_runtime};

use ain_evm::runtime::RUNTIME;
use log::debug;
use std::error::Error;

use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature};
use primitive_types::{H160, H256, U256};
use transaction::{LegacyUnsignedTransaction, TransactionError, LOWER_H256};

pub const WEI_TO_GWEI: u64 = 1_000_000_000;
pub const GWEI_TO_SATS: u64 = 10;

#[cxx::bridge]
pub mod ffi {
    pub struct CreateTransactionContext {
        chain_id: u64,
        nonce: [u8; 32],
        gas_price: [u8; 32],
        gas_limit: [u8; 32],
        to: [u8; 20],
        value: [u8; 32],
        input: Vec<u8>,
        priv_key: [u8; 32],
    }

    pub struct FinalizeBlockResult {
        block_header: Vec<u8>,
        failed_transactions: Vec<String>,
    }

    extern "Rust" {
        fn evm_get_balance(address: &str, block_number: [u8; 32]) -> Result<u64>;
        fn evm_add_balance(
            context: u64,
            address: &str,
            amount: [u8; 32],
            native_tx_hash: [u8; 32],
        ) -> Result<()>;
        fn evm_sub_balance(
            context: u64,
            address: &str,
            amount: [u8; 32],
            native_tx_hash: [u8; 32],
        ) -> Result<bool>;
        fn evm_validate_raw_tx(tx: &str) -> Result<bool>;

        fn evm_get_context() -> u64;
        fn evm_discard_context(context: u64) -> Result<()>;
        fn evm_queue_tx(context: u64, raw_tx: &str, native_tx_hash: [u8; 32]) -> Result<bool>;
        fn evm_finalize(
            context: u64,
            update_state: bool,
            difficulty: u32,
            miner_address: [u8; 20],
        ) -> Result<FinalizeBlockResult>;

        fn init_runtime();
        fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<()>;
        fn stop_runtime();

        fn create_and_sign_tx(ctx: CreateTransactionContext) -> Result<Vec<u8>>;
    }
}

pub fn create_and_sign_tx(ctx: ffi::CreateTransactionContext) -> Result<Vec<u8>, TransactionError> {
    let to_action = if ctx.to.is_empty() {
        TransactionAction::Create
    } else {
        TransactionAction::Call(H160::from_slice(&ctx.to))
    };

    let nonce_u256 = U256::from(ctx.nonce);
    let gas_price_u256 = U256::from(ctx.gas_price);
    let gas_limit_u256 = U256::from(ctx.gas_limit);
    let value_u256 = U256::from(ctx.value);

    // Create
    let t = LegacyUnsignedTransaction {
        nonce: nonce_u256,
        gas_price: gas_price_u256,
        gas_limit: gas_limit_u256,
        action: to_action,
        value: value_u256,
        input: ctx.input,
        // Dummy sig for now. Needs 27, 28 or > 36 for valid v.
        sig: TransactionSignature::new(27, LOWER_H256, LOWER_H256).unwrap(),
    };

    // Sign
    let priv_key_h256 = H256::from(ctx.priv_key);
    let signed = t.sign(&priv_key_h256, ctx.chain_id)?;

    Ok(signed.encode().into())
}

pub fn evm_get_balance(address: &str, block_number: [u8; 32]) -> Result<u64, Box<dyn Error>> {
    let account = address.parse()?;
    let mut balance = RUNTIME
        .handlers
        .evm
        .get_balance(account, U256::from(block_number))
        .unwrap_or_default(); // convert to try_evm_get_balance - Default to 0 for now
    balance /= WEI_TO_GWEI;
    balance /= GWEI_TO_SATS;
    Ok(balance.as_u64())
}

pub fn evm_add_balance(
    context: u64,
    address: &str,
    amount: [u8; 32],
    hash: [u8; 32],
) -> Result<(), Box<dyn Error>> {
    let address = address.parse()?;

    RUNTIME
        .handlers
        .evm
        .add_balance(context, address, amount.into(), hash)?;
    Ok(())
}

pub fn evm_sub_balance(
    context: u64,
    address: &str,
    amount: [u8; 32],
    hash: [u8; 32],
) -> Result<bool, Box<dyn Error>> {
    let address = address.parse()?;
    match RUNTIME
        .handlers
        .evm
        .sub_balance(context, address, amount.into(), hash)
    {
        Ok(_) => Ok(true),
        Err(_) => Ok(false),
    }
}

pub fn evm_validate_raw_tx(tx: &str) -> Result<bool, Box<dyn Error>> {
    match RUNTIME.handlers.evm.validate_raw_tx(tx) {
        Ok(_) => Ok(true),
        Err(e) => {
            debug!("evm_validate_raw_tx fails with error: {:?}", e);
            Ok(false)
        }
    }
}

#[must_use]
pub fn evm_get_context() -> u64 {
    RUNTIME.handlers.evm.get_context()
}

fn evm_discard_context(context: u64) -> Result<(), Box<dyn Error>> {
    // TODO discard
    RUNTIME.handlers.evm.clear(context)?;
    Ok(())
}

fn evm_queue_tx(context: u64, raw_tx: &str, hash: [u8; 32]) -> Result<bool, Box<dyn Error>> {
    let signed_tx: SignedTx = raw_tx.try_into()?;
    match RUNTIME
        .handlers
        .evm
        .queue_tx(context, signed_tx.into(), hash)
    {
        Ok(_) => Ok(true),
        Err(_) => Ok(false),
    }
}

use rlp::Encodable;
fn evm_finalize(
    context: u64,
    update_state: bool,
    difficulty: u32,
    miner_address: [u8; 20],
) -> Result<ffi::FinalizeBlockResult, Box<dyn Error>> {
    let eth_address = H160::from(miner_address);
    let (block, failed_txs) =
        RUNTIME
            .handlers
            .finalize_block(context, update_state, difficulty, Some(eth_address))?;
    Ok(ffi::FinalizeBlockResult {
        block_header: block.header.rlp_bytes().into(),
        failed_transactions: failed_txs,
    })
}
