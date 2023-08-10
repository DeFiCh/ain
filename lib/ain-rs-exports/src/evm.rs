use ain_evm::storage::traits::BlockStorage;
use ain_evm::transaction::system::{DST20Data, DeployContractData, SystemTx};
use ain_evm::txqueue::QueueTx;
use ain_evm::{
    core::ValidateTxInfo,
    evm::FinalizedBlockInfo,
    services::SERVICES,
    storage::traits::Rollback,
    storage::traits::TransactionStorage,
    transaction::{self, SignedTx},
    weiamount::WeiAmount,
};
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
pub fn evm_try_get_balance(result: &mut ffi::CrossBoundaryResult, address: [u8; 20]) -> u64 {
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
    ); // convert to evm_try_get_balance - Default to 0 for now
    let Ok(balance) = u64::try_from(balance.to_satoshi()) else {
        return cross_boundary_error_return(result, "evm balance value overflow");
    };
    cross_boundary_success(result);
    balance
}

/// Retrieves the next valid nonce of an EVM account in a specific queue_id
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `address` - The EVM address of the account.
///
/// # Returns
///
/// Returns the next valid nonce of the account in a specific queue_id as a `u64`
pub fn evm_unsafe_try_get_next_valid_nonce_in_q(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    address: [u8; 20],
) -> u64 {
    let address = H160::from(address);
    unsafe {
        match SERVICES
            .evm
            .core
            .get_next_valid_nonce_in_queue(queue_id, address)
        {
            Ok(nonce) => {
                let Ok(nonce) = u64::try_from(nonce) else {
                    return cross_boundary_error_return(result, "nonce value overflow");
                };
                cross_boundary_success_return(result, nonce)
            }
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

/// Removes all transactions in the queue whose sender matches the provided sender address in a specific queue_id
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `address` - The EVM address of the account.
///
pub fn evm_unsafe_try_remove_txs_by_sender_in_q(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    address: [u8; 20],
) {
    let address = H160::from(address);
    unsafe {
        match SERVICES.evm.core.remove_txs_by_sender_in(queue_id, address) {
            Ok(_) => cross_boundary_success_return(result, ()),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

/// EvmIn. Send DFI to an EVM account.
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `address` - The EVM address of the account.
/// * `amount` - The amount to add as a byte array.
/// * `hash` - The hash value as a byte array.
///
pub fn evm_unsafe_try_add_balance_in_q(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    address: &str,
    amount: [u8; 32],
    hash: [u8; 32],
) {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };

    unsafe {
        match SERVICES
            .evm
            .core
            .add_balance(queue_id, address, amount.into(), hash)
        {
            Ok(_) => cross_boundary_success_return(result, ()),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

/// EvmOut. Send DFI from an EVM account.
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `address` - The EVM address of the account.
/// * `amount` - The amount to subtract as a byte array.
/// * `hash` - The hash value as a byte array.
///
/// # Errors
///
/// Returns an Error if:
/// - the queue_id does not match any existing queue
/// - the address is not a valid EVM address
/// - the account has insufficient balance.
///
/// # Returns
///
/// Returns `true` if the balance subtraction is successful, `false` otherwise.
pub fn evm_unsafe_try_sub_balance_in_q(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    address: &str,
    amount: [u8; 32],
    hash: [u8; 32],
) -> bool {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };

    unsafe {
        match SERVICES
            .evm
            .core
            .sub_balance(queue_id, address, amount.into(), hash)
        {
            Ok(_) => cross_boundary_success_return(result, true),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
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
pub fn evm_unsafe_try_prevalidate_raw_tx(
    result: &mut ffi::CrossBoundaryResult,
    tx: &str,
) -> ffi::PreValidateTxCompletion {
    let queue_id = 0;
    unsafe {
        match SERVICES.evm.core.validate_raw_tx(tx, queue_id) {
            Ok(ValidateTxInfo {
                signed_tx,
                prepay_fee,
                used_gas: _,
            }) => {
                let Ok(nonce) = u64::try_from(signed_tx.nonce()) else {
                    return cross_boundary_error_return(result, "nonce value overflow");
                };

                let Ok(prepay_fee) = u64::try_from(prepay_fee) else {
                    return cross_boundary_error_return(result, "prepay fee value overflow");
                };

                cross_boundary_success_return(
                    result,
                    ffi::PreValidateTxCompletion {
                        nonce,
                        sender: signed_tx.sender.to_fixed_bytes(),
                        prepay_fee,
                    },
                )
            }
            Err(e) => {
                debug!("evm_try_prevalidate_raw_tx failed with error: {e}");
                cross_boundary_error_return(result, e.to_string())
            }
        }
    }
}

/// Validates a raw EVM transaction.
///
/// # Arguments
///
/// * `result` - Result object
/// * `tx` - The raw transaction string.
/// * `queue_id` - The EVM queue ID
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
pub fn evm_unsafe_try_validate_raw_tx_in_q(
    result: &mut ffi::CrossBoundaryResult,
    tx: &str,
    queue_id: u64,
) -> ffi::ValidateTxCompletion {
    match SERVICES.evm.verify_tx_fees(tx) {
        Ok(_) => (),
        Err(e) => {
            debug!("evm_try_validate_raw_tx failed with error: {e}");
            return cross_boundary_error_return(result, e.to_string());
        }
    }
    unsafe {
        match SERVICES.evm.core.validate_raw_tx(tx, queue_id) {
            Ok(ValidateTxInfo {
                signed_tx,
                prepay_fee,
                used_gas,
            }) => {
                let Ok(nonce) = u64::try_from(signed_tx.nonce()) else {
                    return cross_boundary_error_return(result, "nonce value overflow");
                };

                let Ok(prepay_fee) = u64::try_from(prepay_fee) else {
                    return cross_boundary_error_return(result, "prepay fee value overflow");
                };

                cross_boundary_success_return(
                    result,
                    ffi::ValidateTxCompletion {
                        nonce,
                        sender: signed_tx.sender.to_fixed_bytes(),
                        tx_hash: signed_tx.hash().to_fixed_bytes(),
                        prepay_fee,
                        gas_used: used_gas,
                    },
                )
            }
            Err(e) => {
                debug!("evm_try_validate_raw_tx failed with error: {e}");
                cross_boundary_error_return(result, e.to_string())
            }
        }
    }
}

/// Retrieves the EVM queue ID.
///
/// # Returns
///
/// Returns the EVM queue ID as a `u64`.
pub fn evm_unsafe_try_create_queue(result: &mut ffi::CrossBoundaryResult) -> u64 {
    let q;
    unsafe { q = SERVICES.evm.core.create_queue() }
    cross_boundary_success(result);
    q
}

/// /// Discards an EVM queue.
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
///
pub fn evm_unsafe_try_remove_queue(result: &mut ffi::CrossBoundaryResult, queue_id: u64) {
    unsafe { SERVICES.evm.core.remove_queue(queue_id) }
    cross_boundary_success(result);
}

/// Add an EVM transaction to a specific queue.
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `raw_tx` - The raw transaction string.
/// * `hash` - The native transaction hash.
///
/// # Errors
///
/// Returns an Error if:
/// - The `raw_tx` is in invalid format
/// - The queue does not exists.
///
pub fn evm_unsafe_try_push_tx_in_q(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    raw_tx: &str,
    hash: [u8; 32],
    gas_used: u64,
) {
    let signed_tx: Result<SignedTx, TransactionError> = raw_tx.try_into();
    unsafe {
        match signed_tx {
            Ok(signed_tx) => {
                match SERVICES.evm.push_tx_in_queue(
                    queue_id,
                    signed_tx.into(),
                    hash,
                    U256::from(gas_used),
                ) {
                    Ok(_) => cross_boundary_success(result),
                    Err(e) => cross_boundary_error_return(result, e.to_string()),
                }
            }
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

/// Creates an EVM block.
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `difficulty` - The block's difficulty.
/// * `miner_address` - The miner's EVM address as a byte array.
/// * `timestamp` - The block's timestamp.
///
/// # Returns
///
/// Returns a `FinalizeBlockResult` containing the block hash, failed transactions, burnt fees and priority fees (in satoshis) on success.
pub fn evm_unsafe_try_construct_block_in_q(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    difficulty: u32,
    miner_address: [u8; 20],
    timestamp: u64,
    dvm_block_number: u64,
) -> ffi::FinalizeBlockCompletion {
    let eth_address = H160::from(miner_address);
    unsafe {
        match SERVICES.evm.construct_block_in_queue(
            queue_id,
            difficulty,
            eth_address,
            timestamp,
            dvm_block_number,
        ) {
            Ok(FinalizedBlockInfo {
                block_hash,
                failed_transactions,
                total_burnt_fees,
                total_priority_fees,
            }) => {
                let Ok(total_burnt_fees) = u64::try_from(WeiAmount(total_burnt_fees).to_satoshi()) else {
                    return cross_boundary_error_return(result, "total burnt fees value overflow");
                };
                let Ok(total_priority_fees) = u64::try_from(WeiAmount(total_priority_fees).to_satoshi()) else {
                    return cross_boundary_error_return(result, "total priority fees value overflow");
                };
                cross_boundary_success(result);
                ffi::FinalizeBlockCompletion {
                    block_hash,
                    failed_transactions,
                    total_burnt_fees,
                    total_priority_fees,
                }
            }
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

pub fn evm_unsafe_try_commit_queue(result: &mut ffi::CrossBoundaryResult, queue_id: u64) {
    unsafe {
        match SERVICES.evm.commit_queue(queue_id) {
            Ok(_) => cross_boundary_success(result),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

pub fn evm_disconnect_latest_block(result: &mut ffi::CrossBoundaryResult) {
    SERVICES.evm.storage.disconnect_latest_block();
    cross_boundary_success(result);
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
        Some(block) => {
            let Ok(block_number) = u64::try_from(block.header.number) else {
                return cross_boundary_error_return(result, "Block number value overflow");
            };
            cross_boundary_success_return(result, block_number)
        }
        None => cross_boundary_error_return(result, "Invalid block hash"),
    }
}

pub fn evm_try_get_block_count(result: &mut ffi::CrossBoundaryResult) -> u64 {
    match SERVICES.evm.block.get_latest_block_hash_and_number() {
        Some((_, number)) => {
            let Ok(number) = u64::try_from(number) else {
                return cross_boundary_error_return(result, "Count value overflow");
            };
            cross_boundary_success_return(result, number)
        }
        None => cross_boundary_error_return(result, "Unable to get block count"),
    }
}

pub fn evm_try_get_tx_by_hash(
    result: &mut ffi::CrossBoundaryResult,
    tx_hash: [u8; 32],
) -> ffi::EVMTransaction {
    match SERVICES
        .evm
        .storage
        .get_transaction_by_hash(&H256::from(tx_hash))
    {
        Some(tx) => {
            let Ok(tx) = SignedTx::try_from(tx) else {
                return cross_boundary_error_return(result, "failed to convert tx to SignedTx");
            };

            let Ok(nonce) = u64::try_from(tx.nonce()) else {
                return cross_boundary_error_return(result, "tx nonce value overflow");
            };

            let Ok(gas_price) = u64::try_from(WeiAmount(tx.gas_price()).to_satoshi()) else {
                return cross_boundary_error_return(result, "tx gas price value overflow");
            };

            let Ok(gas_limit) = u64::try_from(tx.gas_limit()) else {
                return cross_boundary_error_return(result, "tx gas limit value overflow");
            };

            let Ok(value) = u64::try_from(WeiAmount(tx.value()).to_satoshi()) else {
                return cross_boundary_error_return(result, "tx value overflow");
            };

            let out = ffi::EVMTransaction {
                hash: tx.hash().to_fixed_bytes(),
                sender: tx.sender.to_fixed_bytes(),
                nonce,
                gas_price,
                gas_limit,
                create_tx: match tx.action() {
                    TransactionAction::Call(_) => false,
                    TransactionAction::Create => true,
                },
                to: match tx.to() {
                    Some(to) => to.to_fixed_bytes(),
                    None => H160::zero().to_fixed_bytes(),
                },
                value,
                data: tx.data().to_vec(),
            };
            cross_boundary_success_return(result, out)
        }
        None => cross_boundary_error_return(result, "Unable to get evm tx from tx hash"),
    }
}

pub fn evm_try_create_dst20(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    native_hash: [u8; 32],
    name: &str,
    symbol: &str,
    token_id: &str,
) {
    let address = match ain_contracts::dst20_address_from_token_id(token_id) {
        Ok(address) => address,
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    };
    debug!("Deploying to address {:#?}", address);

    let system_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        name: String::from(name),
        symbol: String::from(symbol),
        address,
    }));

    unsafe {
        match SERVICES
            .evm
            .push_tx_in_queue(queue_id, system_tx, native_hash, U256::zero())
        {
            Ok(_) => cross_boundary_success(result),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

pub fn evm_try_bridge_dst20(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    address: &str,
    amount: [u8; 32],
    native_hash: [u8; 32],
    token_id: &str,
    out: bool,
) {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };
    let contract = ain_contracts::dst20_address_from_token_id(token_id)
        .unwrap_or_else(|e| cross_boundary_error_return(result, e.to_string()));

    let system_tx = QueueTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
        to: address,
        contract,
        amount: amount.into(),
        out,
    }));

    unsafe {
        match SERVICES
            .evm
            .push_tx_in_queue(queue_id, system_tx, native_hash, U256::zero())
        {
            Ok(_) => cross_boundary_success(result),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}
