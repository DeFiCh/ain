mod dst20;

use ain_evm::{
    storage::traits::Rollback,
    transaction::{self, SignedTx},
};
use ain_grpc::{init_evm_runtime, start_evm_servers, stop_evm_runtime};

use ain_evm::runtime::RUNTIME;
use log::debug;

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
        pub chain_id: u64,
        pub nonce: [u8; 32],
        pub gas_price: [u8; 32],
        pub gas_limit: [u8; 32],
        pub to: [u8; 20],
        pub value: [u8; 32],
        pub input: Vec<u8>,
        pub priv_key: [u8; 32],
    }

    #[derive(Default)]
    pub struct FinalizeBlockCompletion {
        pub block_hash: [u8; 32],
        pub failed_transactions: Vec<String>,
        pub miner_fee: u64,
    }

    #[derive(Default)]
    pub struct ValidateTxCompletion {
        pub nonce: u64,
        pub sender: [u8; 20],
        pub used_gas: u64,
    }

    pub struct CrossBoundaryResult {
        pub ok: bool,
        pub reason: String,
    }

    extern "Rust" {
        fn evm_get_balance(address: [u8; 20]) -> u64;

        fn evm_get_next_valid_nonce_in_context(context: u64, address: [u8; 20]) -> u64;

        fn evm_remove_txs_by_sender(context: u64, address: [u8; 20]);

        fn evm_add_balance(context: u64, address: &str, amount: [u8; 32], native_tx_hash: [u8; 32]);
        fn evm_sub_balance(
            context: u64,
            address: &str,
            amount: [u8; 32],
            native_tx_hash: [u8; 32],
        ) -> bool;

        fn evm_try_prevalidate_raw_tx(
            result: &mut CrossBoundaryResult,
            tx: &str,
            with_gas_usage: bool,
        ) -> ValidateTxCompletion;

        fn evm_get_context() -> u64;
        fn evm_discard_context(context: u64);
        fn evm_try_queue_tx(
            result: &mut CrossBoundaryResult,
            context: u64,
            raw_tx: &str,
            native_tx_hash: [u8; 32],
        );

        fn evm_try_finalize(
            result: &mut CrossBoundaryResult,
            context: u64,
            update_state: bool,
            difficulty: u32,
            miner_address: [u8; 20],
            timestamp: u64,
        ) -> FinalizeBlockCompletion;

        fn preinit();
        fn init_evm_runtime();
        fn start_servers(result: &mut CrossBoundaryResult, json_addr: &str, grpc_addr: &str);
        fn stop_evm_runtime();

        fn create_and_sign_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransactionContext,
        ) -> Vec<u8>;

        fn evm_disconnect_latest_block();

        fn create_dst20(
            result: &mut CrossBoundaryResult,
            context: u64,
            native_hash: [u8; 32],
            name: &str,
            symbol: &str,
            token_id: &str,
        ) -> bool;
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
pub fn create_and_sign_tx(
    result: &mut CrossBoundaryResult,
    ctx: ffi::CreateTransactionContext,
) -> Vec<u8> {
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
    match t.sign(&priv_key_h256, ctx.chain_id) {
        Ok(signed) => {
            result.ok = true;
            signed.encode().into()
        }
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
            Vec::new()
        }
    }
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
pub fn evm_get_balance(address: [u8; 20]) -> u64 {
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
    balance.as_u64()
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
pub fn evm_remove_txs_by_sender(context: u64, address: [u8; 20]) {
    let address = H160::from(address);
    let _ = RUNTIME.handlers.evm.remove_txs_by_sender(context, address);
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
pub fn evm_add_balance(context: u64, address: &str, amount: [u8; 32], hash: [u8; 32]) {
    if let Ok(address) = address.parse() {
        let _ = RUNTIME
            .handlers
            .evm
            .add_balance(context, address, amount.into(), hash);
    }
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
pub fn evm_sub_balance(context: u64, address: &str, amount: [u8; 32], hash: [u8; 32]) -> bool {
    if let Ok(address) = address.parse() {
        if let Ok(()) = RUNTIME
            .handlers
            .evm
            .sub_balance(context, address, amount.into(), hash)
        {
            return true;
        }
    }
    false
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
) -> ffi::ValidateTxCompletion {
    match RUNTIME.handlers.evm.validate_raw_tx(tx, with_gas_usage) {
        Ok((signed_tx, used_gas)) => {
            result.ok = true;

            ffi::ValidateTxCompletion {
                nonce: signed_tx.nonce().as_u64(),
                sender: signed_tx.sender.to_fixed_bytes(),
                used_gas,
            }
        }
        Err(e) => {
            debug!("evm_try_prevalidate_raw_tx failed with error: {e}");
            result.ok = false;
            result.reason = e.to_string();

            ffi::ValidateTxCompletion::default()
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
fn evm_try_queue_tx(result: &mut CrossBoundaryResult, context: u64, raw_tx: &str, hash: [u8; 32]) {
    let signed_tx: Result<SignedTx, TransactionError> = raw_tx.try_into();
    match signed_tx {
        Ok(signed_tx) => match RUNTIME.handlers.queue_tx(context, signed_tx.into(), hash) {
            Ok(_) => {
                result.ok = true;
            }
            Err(e) => {
                result.ok = false;
                result.reason = e.to_string();
            }
        },
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
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
) -> ffi::FinalizeBlockCompletion {
    let eth_address = H160::from(miner_address);
    match RUNTIME
        .handlers
        .finalize_block(context, update_state, difficulty, eth_address, timestamp)
    {
        Ok((block_hash, failed_txs, gas_used)) => {
            result.ok = true;
            ffi::FinalizeBlockCompletion {
                block_hash,
                failed_transactions: failed_txs,
                miner_fee: gas_used,
            }
        }
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
            ffi::FinalizeBlockCompletion::default()
        }
    }
}

pub fn preinit() {
    ain_grpc::preinit();
}

fn evm_disconnect_latest_block() {
    RUNTIME.handlers.storage.disconnect_latest_block();
}

fn start_servers(result: &mut CrossBoundaryResult, json_addr: &str, grpc_addr: &str) {
    match start_evm_servers(json_addr, grpc_addr) {
        Ok(()) => {
            result.ok = true;
        }
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
        }
    }
}

fn create_dst20(
    result: &mut CrossBoundaryResult,
    context: u64,
    native_hash: [u8; 32],
    name: &str,
    symbol: &str,
    token_id: &str,
) -> bool {
    match deploy_dst20(
        native_hash,
        context,
        String::from(name),
        String::from(symbol),
        String::from(token_id),
    ) {
        Ok(_) => true,
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();

            false
        }
    }
}
