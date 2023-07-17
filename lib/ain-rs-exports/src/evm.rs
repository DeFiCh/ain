use ain_evm::{
    evm::FinalizedBlockInfo,
    services::SERVICES,
    storage::traits::Rollback,
    transaction::{self, SignedTx},
};

use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature};
use log::debug;
use primitive_types::{H160, H256, U256};
use transaction::{LegacyUnsignedTransaction, TransactionError, LOWER_H256};

use crate::ffi;

pub const WEI_TO_GWEI: u64 = 1_000_000_000;
pub const GWEI_TO_SATS: u64 = 10;

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
pub fn evm_try_create_and_sign_tx(
    result: &mut ffi::CrossBoundaryResult,
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
    let (_, latest_block_number) = SERVICES
        .evm
        .block
        .get_latest_block_hash_and_number()
        .unwrap_or_default();
    let mut balance = SERVICES
        .evm
        .core
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
    let nonce = SERVICES
        .evm
        .core
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
    let _ = SERVICES.evm.core.remove_txs_by_sender(context, address);
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
        let _ = SERVICES
            .evm
            .core
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
        if let Ok(()) = SERVICES
            .evm
            .core
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
/// - Transaction gas fee is lower than the next block's base fee
///
/// # Returns
///
/// Returns the transaction nonce, sender address and gas used if the transaction is valid.
/// logs and set the error reason to result object otherwise.
pub fn evm_try_prevalidate_raw_tx(
    result: &mut ffi::CrossBoundaryResult,
    tx: &str,
    call_tx: bool,
    context: u64,
) -> ffi::ValidateTxCompletion {
    match SERVICES.evm.verify_tx_fees(tx) {
        Ok(_) => (),
        Err(e) => {
            debug!("evm_try_prevalidate_raw_tx failed with error: {e}");
            result.ok = false;
            result.reason = e.to_string();

            return ffi::ValidateTxCompletion::default();
        }
    }

    match SERVICES.evm.core.validate_raw_tx(tx, call_tx, context) {
        Ok((signed_tx, tx_fees, gas_used)) => {
            result.ok = true;

            ffi::ValidateTxCompletion {
                nonce: signed_tx.nonce().as_u64(),
                sender: signed_tx.sender.to_fixed_bytes(),
                tx_fees: tx_fees.try_into().unwrap_or_default(),
                gas_used,
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
pub fn evm_get_context() -> u64 {
    SERVICES.evm.core.get_context()
}

/// /// Discards an EVM context queue.
///
/// # Arguments
///
/// * `context` - The context queue number.
///
pub fn evm_discard_context(context: u64) {
    SERVICES.evm.core.remove(context)
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
pub fn evm_try_queue_tx(
    result: &mut ffi::CrossBoundaryResult,
    context: u64,
    raw_tx: &str,
    hash: [u8; 32],
    gas_used: u64,
) {
    let signed_tx: Result<SignedTx, TransactionError> = raw_tx.try_into();
    match signed_tx {
        Ok(signed_tx) => {
            match SERVICES
                .evm
                .queue_tx(context, signed_tx.into(), hash, gas_used)
            {
                Ok(_) => {
                    result.ok = true;
                }
                Err(e) => {
                    result.ok = false;
                    result.reason = e.to_string();
                }
            }
        }
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
/// Returns a `FinalizeBlockResult` containing the block hash, failed transactions, burnt fees and priority fees (in satoshis) on success.
pub fn evm_try_finalize(
    result: &mut ffi::CrossBoundaryResult,
    context: u64,
    update_state: bool,
    difficulty: u32,
    miner_address: [u8; 20],
    timestamp: u64,
) -> ffi::FinalizeBlockCompletion {
    let eth_address = H160::from(miner_address);
    match SERVICES
        .evm
        .finalize_block(context, update_state, difficulty, eth_address, timestamp)
    {
        Ok(FinalizedBlockInfo {
            block_hash,
            failed_transactions,
            total_burnt_fees,
            total_priority_fees,
        }) => {
            result.ok = true;
            ffi::FinalizeBlockCompletion {
                block_hash,
                failed_transactions,
                total_burnt_fees: total_burnt_fees / WEI_TO_GWEI / GWEI_TO_SATS,
                total_priority_fees: total_priority_fees / WEI_TO_GWEI / GWEI_TO_SATS,
            }
        }
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
            ffi::FinalizeBlockCompletion::default()
        }
    }
}

pub fn evm_disconnect_latest_block() {
    SERVICES.evm.storage.disconnect_latest_block();
}
