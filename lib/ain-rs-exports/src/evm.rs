use ain_evm::{
    core::ValidateTxInfo,
    evm::FinalizedBlockInfo,
    services::SERVICES,
    storage::traits::Rollback,
    transaction::{self, SignedTx},
    weiamount::WeiAmount,
};

use ain_evm::storage::traits::BlockStorage;
use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature};
use log::debug;
use primitive_types::{H160, H256, U256};
use transaction::{LegacyUnsignedTransaction, TransactionError, LOWER_H256};

use crate::ffi;
use crate::prelude::*;

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
        Ok(signed) => cross_boundary_success_return(result, signed.encode().into()),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
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
    let balance = WeiAmount(
        SERVICES
            .evm
            .core
            .get_balance(account, latest_block_number)
            .unwrap_or_default(),
    ); // convert to try_evm_get_balance - Default to 0 for now
    balance.to_satoshi().as_u64()
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

/// Pre-validates a raw EVM transaction.
///
/// # Arguments
///
/// * `result` - Result object
/// * `tx` - The raw transaction string.
///
/// # Errors
///
/// Returns an Error if:
/// - The hex data is invalid
/// - The EVM transaction is invalid
/// - The EVM transaction fee is lower than initial block base fee
/// - Could not fetch the underlying EVM account
/// - Account's nonce does not match raw tx's nonce
/// - The EVM transaction prepay gas is invalid
/// - The EVM transaction gas limit is lower than the transaction intrinsic gas
///
/// # Returns
///
/// Returns the transaction nonce, sender address and transaction fees if valid.
/// Logs and set the error reason to result object otherwise.
pub fn evm_try_prevalidate_raw_tx(
    result: &mut ffi::CrossBoundaryResult,
    tx: &str,
) -> ffi::PreValidateTxCompletion {
    match SERVICES.evm.verify_tx_fees(tx, false) {
        Ok(_) => (),
        Err(e) => {
            debug!("evm_try_prevalidate_raw_tx failed with error: {e}");
            return cross_boundary_error_return(result, e.to_string());
        }
    }

    let context = 0;
    match SERVICES.evm.core.validate_raw_tx(tx, context, false) {
        Ok(ValidateTxInfo {
            signed_tx,
            prepay_fee,
            used_gas: _,
        }) => cross_boundary_success_return(
            result,
            ffi::PreValidateTxCompletion {
                nonce: signed_tx.nonce().as_u64(),
                sender: signed_tx.sender.to_fixed_bytes(),
                tx_fees: prepay_fee.try_into().unwrap_or_default(),
            },
        ),
        Err(e) => {
            debug!("evm_try_prevalidate_raw_tx failed with error: {e}");
            cross_boundary_error_return(result, e.to_string())
        }
    }
}

/// Validates a raw EVM transaction.
///
/// # Arguments
///
/// * `result` - Result object
/// * `tx` - The raw transaction string.
/// * `context` - The EVM txqueue unique key
///
/// # Errors
///
/// Returns an Error if:
/// - The hex data is invalid
/// - The EVM transaction is invalid
/// - The EVM transaction fee is lower than the next block's base fee
/// - Could not fetch the underlying EVM account
/// - Account's nonce does not match raw tx's nonce
/// - The EVM transaction prepay gas is invalid
/// - The EVM transaction gas limit is lower than the transaction intrinsic gas
/// - The EVM transaction call failed
///
/// # Returns
///
/// Returns the transaction nonce, sender address, transaction fees and gas used
/// if valid. Logs and set the error reason to result object otherwise.
pub fn evm_try_validate_raw_tx(
    result: &mut ffi::CrossBoundaryResult,
    tx: &str,
    context: u64,
) -> ffi::ValidateTxCompletion {
    match SERVICES.evm.verify_tx_fees(tx, true) {
        Ok(_) => (),
        Err(e) => {
            debug!("evm_try_validate_raw_tx failed with error: {e}");
            return cross_boundary_error_return(result, e.to_string());
        }
    }

    match SERVICES.evm.core.validate_raw_tx(tx, context, true) {
        Ok(ValidateTxInfo {
            signed_tx,
            prepay_fee,
            used_gas,
        }) => cross_boundary_success_return(
            result,
            ffi::ValidateTxCompletion {
                nonce: signed_tx.nonce().as_u64(),
                sender: signed_tx.sender.to_fixed_bytes(),
                tx_fees: prepay_fee.try_into().unwrap_or_default(),
                gas_used: used_gas,
            },
        ),
        Err(e) => {
            debug!("evm_try_validate_raw_tx failed with error: {e}");
            cross_boundary_error_return(result, e.to_string())
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
                Ok(_) => cross_boundary_success(result),
                Err(e) => cross_boundary_error_return(result, e.to_string()),
            }
        }
        Err(e) => cross_boundary_error_return(result, e.to_string()),
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
                total_burnt_fees: WeiAmount(total_burnt_fees).to_satoshi().as_u64(),
                total_priority_fees: WeiAmount(total_priority_fees).to_satoshi().as_u64(),
            }
        }
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn evm_disconnect_latest_block() {
    SERVICES.evm.storage.disconnect_latest_block();
}

/// Return the block for a given height.
///
/// # Arguments
///
/// * `height` - The block number we want to get the blockhash from.
///
/// # Returns
///
/// Returns the blockhash associated with the given block number.
pub fn evm_try_get_block_hash_by_number(
    result: &mut ffi::CrossBoundaryResult,
    height: u64,
) -> [u8; 32] {
    match SERVICES
        .evm
        .storage
        .get_block_by_number(&U256::from(height))
    {
        Some(block) => cross_boundary_success_return(result, block.header.hash().to_fixed_bytes()),
        None => cross_boundary_error_return(result, "Invalid block number"),
    }
}

/// Return the block number for a given blockhash.
///
/// # Arguments
///
/// * `hash` - The hash of the block we want to get the block number.
///
/// # Returns
///
/// Returns the block number associated with the given blockhash.
pub fn evm_try_get_block_number_by_hash(
    result: &mut ffi::CrossBoundaryResult,
    hash: [u8; 32],
) -> u64 {
    match SERVICES.evm.storage.get_block_by_hash(&H256::from(hash)) {
        Some(block) => cross_boundary_success_return(result, block.header.number.as_u64()),
        None => cross_boundary_error_return(result, "Invalid block hash"),
    }
}
