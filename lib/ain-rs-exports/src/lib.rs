mod dst20;

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

use crate::dst20::deploy_dst20;
use crate::ffi::CrossBoundaryResult;

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

    #[derive(Default)]
    pub struct FinalizeBlockResult {
        block_hash: [u8; 32],
        failed_transactions: Vec<String>,
        miner_fee: u64,
    }

    #[derive(Default)]
    pub struct ValidateTxResult {
        pub nonce: u64,
        pub sender: [u8; 20],
        pub used_gas: u64,
    }

    pub struct CrossBoundaryResult {
        pub ok: bool,
        pub reason: String,
    }

    extern "Rust" {
        fn evm_get_balance(address: [u8; 20]) -> Result<u64>;
        fn evm_get_nonce(address: [u8; 20]) -> Result<u64>;
        fn evm_get_next_valid_nonce_in_context(context: u64, address: [u8; 20]) -> u64;

        fn evm_remove_txs_by_sender(context: u64, address: [u8; 20]) -> Result<()>;

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

        fn evm_try_prevalidate_raw_tx(
            result: &mut CrossBoundaryResult,
            tx: &str,
            with_gas_usage: bool,
        ) -> ValidateTxResult;

        fn evm_get_context() -> u64;
        fn evm_discard_context(context: u64);
        fn evm_try_queue_tx(
            result: &mut CrossBoundaryResult,
            context: u64,
            raw_tx: &str,
            native_tx_hash: [u8; 32],
        ) -> Result<bool>;

        fn evm_try_finalize(
            result: &mut CrossBoundaryResult,
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

        fn create_dst20(
            result: &mut CrossBoundaryResult,
            native_hash: [u8; 32],
            name: &str,
            symbol: &str,
        ) -> Result<bool>;
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

/// Retrieves the balance of an EVM account at latest block height.
///
/// # Arguments
///
/// * `address` - The EVM address of the account.
///
/// # Errors
///
/// Returns an Error if the address is not a valid EVM address.
///
/// # Returns
///
/// Returns the balance of the account as a `u64` on success.
pub fn evm_get_balance(address: [u8; 20]) -> Result<u64, Box<dyn Error>> {
    let account = H160::from(address);
    let (_, latest_block_number) = RUNTIME
        .handlers
        .block
        .get_latest_block_hash_and_number()
        .unwrap_or_default();
    let mut balance = RUNTIME
        .handlers
        .evm
        .get_balance(account, latest_block_number)
        .unwrap_or_default(); // convert to try_evm_get_balance - Default to 0 for now
    balance /= WEI_TO_GWEI;
    balance /= GWEI_TO_SATS;
    Ok(balance.as_u64())
}

/// Retrieves the nonce of an EVM account at latest block height.
///
/// # Arguments
///
/// * `address` - The EVM address of the account.
///
/// # Errors
///
/// Throws an Error if the address is not a valid EVM address.
///
/// # Returns
///
/// Returns the nonce of the account as a `u64` on success.
pub fn evm_get_nonce(address: [u8; 20]) -> Result<u64, Box<dyn Error>> {
    let account = H160::from(address);
    let (_, latest_block_number) = RUNTIME
        .handlers
        .block
        .get_latest_block_hash_and_number()
        .unwrap_or_default();
    let nonce = RUNTIME
        .handlers
        .evm
        .get_nonce(account, latest_block_number)
        .unwrap_or_default();
    Ok(nonce.as_u64())
}

/// Retrieves the next valid nonce of an EVM account in a specific context
///
/// # Arguments
///
/// * `context` - The context queue number.
/// * `address` - The EVM address of the account.
///
/// # Returns
///
/// Returns the next valid nonce of the account in a specific context as a `u64`
pub fn evm_get_next_valid_nonce_in_context(context: u64, address: [u8; 20]) -> u64 {
    let address = H160::from(address);
    let nonce = RUNTIME
        .handlers
        .evm
        .get_next_valid_nonce_in_context(context, address);
    nonce.as_u64()
}

/// Removes all transactions in the queue whose sender matches the provided sender address in a specific context
///
/// # Arguments
///
/// * `context` - The context queue number.
/// * `address` - The EVM address of the account.
///
/// /// # Errors
///
/// Returns an Error if the context does not match any existing queue
///
pub fn evm_remove_txs_by_sender(context: u64, address: [u8; 20]) -> Result<(), Box<dyn Error>> {
    let address = H160::from(address);
    RUNTIME
        .handlers
        .evm
        .remove_txs_by_sender(context, address)?;
    Ok(())
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
/// * `result` - Result object
/// * `tx` - The raw transaction string.
/// * `with_gas_usage` - Whether to calculate tx gas usage
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
/// Returns the transaction nonce, sender address and gas used if the transaction is valid.
/// logs and set the error reason to result object otherwise.
pub fn evm_try_prevalidate_raw_tx(
    result: &mut CrossBoundaryResult,
    tx: &str,
    with_gas_usage: bool,
) -> ffi::ValidateTxResult {
    match RUNTIME.handlers.evm.validate_raw_tx(tx, with_gas_usage) {
        Ok((signed_tx, used_gas)) => {
            result.ok = true;
            return ffi::ValidateTxResult {
                nonce: signed_tx.nonce().as_u64(),
                sender: signed_tx.sender.to_fixed_bytes(),
                used_gas,
            };
        }
        Err(e) => {
            debug!("evm_try_prevalidate_raw_tx failed with error: {e}");
            result.ok = false;
            result.reason = e.to_string();
            return ffi::ValidateTxResult::default();
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
fn evm_discard_context(context: u64) {
    RUNTIME.handlers.evm.remove(context)
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
fn evm_try_queue_tx(
    result: &mut CrossBoundaryResult,
    context: u64,
    raw_tx: &str,
    hash: [u8; 32],
) -> Result<bool, Box<dyn Error>> {
    let signed_tx: SignedTx = raw_tx.try_into()?;
    match RUNTIME.handlers.queue_tx(context, signed_tx.into(), hash) {
        Ok(_) => {
            result.ok = true;
            Ok(true)
        }
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
            Ok(false)
        }
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
/// Returns an Error if the block has invalid TXs, viz. out of order nonces
///
/// # Returns
///
/// Returns a `FinalizeBlockResult` containing the block hash, failed transactions, and miner fee on success.
fn evm_try_finalize(
    result: &mut CrossBoundaryResult,
    context: u64,
    update_state: bool,
    difficulty: u32,
    miner_address: [u8; 20],
    timestamp: u64,
) -> Result<ffi::FinalizeBlockResult, Box<dyn Error>> {
    let eth_address = H160::from(miner_address);
    match RUNTIME
        .handlers
        .finalize_block(context, update_state, difficulty, eth_address, timestamp)
    {
        Ok((block_hash, failed_txs, gas_used)) => {
            result.ok = true;
            Ok(ffi::FinalizeBlockResult {
                block_hash,
                failed_transactions: failed_txs,
                miner_fee: gas_used,
            })
        }
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
            Ok(ffi::FinalizeBlockResult::default())
        }
    }
}

pub fn preinit() {
    ain_grpc::preinit();
}

fn evm_disconnect_latest_block() -> Result<(), Box<dyn Error>> {
    RUNTIME.handlers.storage.disconnect_latest_block();
    Ok(())
}

fn create_dst20(
    result: &mut CrossBoundaryResult,
    native_hash: [u8; 32],
    name: &str,
    symbol: &str,
) -> Result<bool, Box<dyn Error>> {
    debug!("HERE");
    match deploy_dst20(native_hash, String::from(name), String::from(symbol)) {
        Ok(_) => Ok(true),
        Err(e) => {
            debug!("{:#?}", e);
            result.ok = false;
            result.reason = e.to_string();

            Ok(false)
        }
    }
}
