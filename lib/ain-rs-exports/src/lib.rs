use ain_evm::{
    storage::traits::Rollback,
    transaction::{self, SignedTx},
};
use ain_grpc::{init_evm_runtime, start_servers, stop_evm_runtime};

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
        block_hash: [u8; 32],
        failed_transactions: Vec<String>,
        miner_fee: u64,
    }

    pub struct ValidateTxResult {
        nonce: u64,
        sender: [u8; 20],
    }

    extern "Rust" {
        fn evm_get_balance(address: &str, block_number: [u8; 32]) -> Result<u64>;
        fn evm_get_nonce(address: &str, block_number: [u8; 32]) -> Result<u64>;

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

        fn evm_prevalidate_raw_tx(tx: &str) -> Result<ValidateTxResult>;

        fn evm_get_context() -> u64;
        fn evm_discard_context(context: u64) -> Result<()>;
        fn evm_queue_tx(context: u64, raw_tx: &str, native_tx_hash: [u8; 32]) -> Result<bool>;

        fn evm_finalize(
            context: u64,
            update_state: bool,
            difficulty: u32,
            miner_address: [u8; 20],
            timestamp: u64,
        ) -> Result<FinalizeBlockResult>;

        fn preinit();
        fn init_evm_runtime();
        fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<()>;
        fn stop_evm_runtime();

        fn create_and_sign_tx(ctx: CreateTransactionContext) -> Result<Vec<u8>>;

        fn evm_disconnect_latest_block() -> Result<()>;
    }
}

/// Creates and signs a transaction.
///
/// # Arguments
///
/// * `ctx` - The transaction context.
///
/// # Errors
///
/// Returns a `TransactionError` if signing fails.
///
/// # Returns
///
/// Returns the signed transaction encoded as a byte vector on success.
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

/// Retrieves the balance of an EVM account at a specific block number.
///
/// # Arguments
///
/// * `address` - The EVM address of the account.
/// * `block_number` - The block number as a byte array.
///
/// # Errors
///
/// Returns an Error if the address is not a valid EVM address.
///
/// # Returns
///
/// Returns the balance of the account as a `u64` on success.
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

/// Retrieves the nonce of an EVM account at a specific block number.
///
/// # Arguments
///
/// * `address` - The EVM address of the account.
/// * `block_number` - The block number as a byte array.
///
/// # Errors
///
/// Throws an Error if the address is not a valid EVM address.
///
/// # Returns
///
/// Returns the nonce of the account as a `u64` on success.
pub fn evm_get_nonce(address: &str, block_number: [u8; 32]) -> Result<u64, Box<dyn Error>> {
    let account = address.parse()?;
    let nonce = RUNTIME
        .handlers
        .evm
        .get_nonce(account, U256::from(block_number))
        .unwrap_or_default(); // convert to try_evm_get_balance - Default to 0 for now
    Ok(nonce.as_u64())
}

/// EvmIn. Send DFI to an EVM account.
///
/// # Arguments
///
/// * `context` - The context queue number.
/// * `address` - The EVM address of the account.
/// * `amount` - The amount to add as a byte array.
/// * `hash` - The hash value as a byte array.
///
/// # Errors
///
/// Returns an Error if:
/// - the context does not match any existing queue
/// - the address is not a valid EVM address
///
/// # Returns
///
/// Returns `Ok(())` on success.
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

/// EvmOut. Send DFI from an EVM account.
///
/// # Arguments
///
/// * `context` - The context queue number.
/// * `address` - The EVM address of the account.
/// * `amount` - The amount to subtract as a byte array.
/// * `hash` - The hash value as a byte array.
///
/// # Errors
///
/// Returns an Error if:
/// - the context does not match any existing queue
/// - the address is not a valid EVM address
/// - the account has insufficient balance.
///
/// # Returns
///
/// Returns `true` if the balance subtraction is successful, `false` otherwise.
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

/// Validates a raw EVM transaction.
///
/// # Arguments
///
/// * `tx` - The raw transaction string.
///
/// # Errors
///
/// Returns an Error if:
/// - The hex data is invalid
/// - The EVM transaction is invalid
/// - Could not fetch the underlying EVM account
/// - Account's nonce does not match raw tx's nonce
///
/// # Returns
///
/// Returns the transaction nonce and sender address if the transaction is valid, logs and throws the error otherwise.
pub fn evm_prevalidate_raw_tx(tx: &str) -> Result<ffi::ValidateTxResult, Box<dyn Error>> {
    match RUNTIME.handlers.evm.validate_raw_tx(tx) {
        Ok(signed_tx) => Ok(ffi::ValidateTxResult {
            nonce: signed_tx.nonce().as_u64(),
            sender: signed_tx.sender.to_fixed_bytes(),
        }),
        Err(e) => {
            debug!("evm_prevalidate_raw_tx fails with error: {e}");
            Err(e)
        }
    }
}

/// Retrieves the EVM context queue.
///
/// # Returns
///
/// Returns the EVM context queue number as a `u64`.
#[must_use]
pub fn evm_get_context() -> u64 {
    RUNTIME.handlers.evm.get_context()
}

/// /// Discards an EVM context queue.
///
/// # Arguments
///
/// * `context` - The context queue number.
///
/// # Errors
///
/// Returns an Error if the context does not match any existing queue.
/// # Returns
///
/// Returns `Ok(())` on success.
fn evm_discard_context(context: u64) -> Result<(), Box<dyn Error>> {
    // TODO discard
    RUNTIME.handlers.evm.clear(context)?;
    Ok(())
}

/// Add an EVM transaction to a specific queue.
///
/// # Arguments
///
/// * `context` - The context queue number.
/// * `raw_tx` - The raw transaction string.
/// * `hash` - The native transaction hash.
///
/// # Errors
///
/// Returns an Error if:
/// - The `raw_tx` is in invalid format
/// - The queue does not exists.
///
/// # Returns
///
/// Returns `true` if the transaction is successfully queued, `false` otherwise.
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

/// Finalizes and mine an EVM block.
///
/// # Arguments
///
/// * `context` - The context queue number.
/// * `update_state` - A flag indicating whether to update the state.
/// * `difficulty` - The block's difficulty.
/// * `miner_address` - The miner's EVM address as a byte array.
/// * `timestamp` - The block's timestamp.
///
/// # Errors
///
/// Returns an Error if there is an error restoring the state trie.
///
/// # Returns
///
/// Returns a `FinalizeBlockResult` containing the block hash, failed transactions, and miner fee on success.
fn evm_finalize(
    context: u64,
    update_state: bool,
    difficulty: u32,
    miner_address: [u8; 20],
    timestamp: u64,
) -> Result<ffi::FinalizeBlockResult, Box<dyn Error>> {
    let eth_address = H160::from(miner_address);
    let (block_hash, failed_txs, gas_used) = RUNTIME.handlers.finalize_block(
        context,
        update_state,
        difficulty,
        eth_address,
        timestamp,
    )?;
    Ok(ffi::FinalizeBlockResult {
        block_hash,
        failed_transactions: failed_txs,
        miner_fee: gas_used,
    })
}

pub fn preinit() {
    ain_grpc::preinit();
}

fn evm_disconnect_latest_block() -> Result<(), Box<dyn Error>> {
    RUNTIME.handlers.storage.disconnect_latest_block();
    Ok(())
}
