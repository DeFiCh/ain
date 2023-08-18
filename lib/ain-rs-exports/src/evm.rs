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
    weiamount::{try_from_gwei, try_from_satoshi, WeiAmount},
};
use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature, TransactionV2};
use log::debug;
use primitive_types::U256;
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
        let Ok(to_address) = ctx.to.parse() else {
            return cross_boundary_error_return(result, "Invalid address");
        };
        TransactionAction::Call(to_address)
    };
    let nonce = U256::from(ctx.nonce);
    let gas_price = match try_from_gwei(U256::from(ctx.gas_price)) {
        Ok(price) => price,
        Err(e) => return cross_boundary_error_return(result, e.to_string()),
    };
    let gas_limit = U256::from(ctx.gas_limit);
    let value = match try_from_satoshi(U256::from(ctx.value)) {
        Ok(wei_value) => wei_value,
        Err(e) => return cross_boundary_error_return(result, e.to_string()),
    };

    // Create
    let t = LegacyUnsignedTransaction {
        nonce,
        gas_price: gas_price.0,
        gas_limit,
        action: to_action,
        value: value.0,
        input: ctx.input,
        // Dummy sig for now. Needs 27, 28 or > 36 for valid v.
        sig: TransactionSignature::new(27, LOWER_H256, LOWER_H256).unwrap(),
    };

    // Sign with a big endian byte array
    match t.sign(&ctx.priv_key, ctx.chain_id) {
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
pub fn evm_try_get_balance(result: &mut ffi::CrossBoundaryResult, address: &str) -> u64 {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };
    let (_, latest_block_number) = match SERVICES.evm.block.get_latest_block_hash_and_number() {
        Err(e) => return cross_boundary_error_return(result, e.to_string()),
        Ok(data) => data.unwrap_or_default(),
    };

    match SERVICES.evm.core.get_balance(address, latest_block_number) {
        Err(e) => cross_boundary_error_return(result, e.to_string()),
        Ok(balance) => {
            let amount = WeiAmount(balance).to_satoshi().try_into();

            try_cross_boundary_return(result, amount)
        }
    }
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
    address: &str,
) -> u64 {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };

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
    address: &str,
) {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };

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
    amount: u64,
    native_hash: &str,
) {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };
    let amount = match try_from_satoshi(U256::from(amount)) {
        Ok(wei_amount) => wei_amount,
        Err(e) => return cross_boundary_error_return(result, e.to_string()),
    };
    let native_hash = String::from(native_hash);

    unsafe {
        match SERVICES
            .evm
            .core
            .add_balance(queue_id, address, amount.0, native_hash)
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
    amount: u64,
    native_hash: &str,
) -> bool {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };
    let amount = match try_from_satoshi(U256::from(amount)) {
        Ok(wei_amount) => wei_amount,
        Err(e) => return cross_boundary_error_return(result, e.to_string()),
    };
    let native_hash = String::from(native_hash);

    unsafe {
        match SERVICES
            .evm
            .core
            .sub_balance(queue_id, address, amount.0, native_hash)
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
) -> ffi::ValidateTxCompletion {
    let queue_id = 0;

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
                        sender: format!("{:?}", signed_tx.sender),
                        tx_hash: format!("{:?}", signed_tx.hash()),
                        prepay_fee,
                        gas_used: used_gas,
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
                        sender: format!("{:?}", signed_tx.sender),
                        tx_hash: format!("{:?}", signed_tx.hash()),
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
    unsafe {
        match SERVICES.evm.core.create_queue() {
            Ok(queue_id) => cross_boundary_success_return(result, queue_id),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
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
    native_hash: &str,
    gas_used: u64,
) {
    let native_hash = native_hash.to_string();
    let signed_tx: Result<SignedTx, TransactionError> = raw_tx.try_into();

    unsafe {
        match signed_tx {
            Ok(signed_tx) => {
                match SERVICES.evm.push_tx_in_queue(
                    queue_id,
                    signed_tx.into(),
                    native_hash,
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
    miner_address: &str,
    timestamp: u64,
    dvm_block_number: u64,
    mnview_ptr: usize,
) -> ffi::FinalizeBlockCompletion {
    let Ok(eth_address) = miner_address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };

    unsafe {
        match SERVICES.evm.construct_block_in_queue(
            queue_id,
            difficulty,
            eth_address,
            timestamp,
            dvm_block_number,
            mnview_ptr,
        ) {
            Ok(FinalizedBlockInfo {
                block_hash,
                failed_transactions,
                total_burnt_fees,
                total_priority_fees,
                block_number,
            }) => {
                let Ok(total_burnt_fees) = u64::try_from(WeiAmount(total_burnt_fees).to_satoshi())
                else {
                    return cross_boundary_error_return(result, "total burnt fees value overflow");
                };
                let Ok(total_priority_fees) =
                    u64::try_from(WeiAmount(total_priority_fees).to_satoshi())
                else {
                    return cross_boundary_error_return(
                        result,
                        "total priority fees value overflow",
                    );
                };
                cross_boundary_success(result);
                ffi::FinalizeBlockCompletion {
                    block_hash,
                    failed_transactions,
                    total_burnt_fees,
                    total_priority_fees,
                    block_number: block_number.as_u64(),
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

pub fn evm_try_disconnect_latest_block(result: &mut ffi::CrossBoundaryResult) {
    match SERVICES.evm.storage.disconnect_latest_block() {
        Ok(_) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn evm_try_set_attribute(
    result: &mut ffi::CrossBoundaryResult,
    _queue_id: u64,
    _attribute_type: u32,
    _value: u64,
) -> bool {
    cross_boundary_success_return(result, true)
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
) -> String {
    match SERVICES
        .evm
        .storage
        .get_block_by_number(&U256::from(height))
    {
        Ok(Some(block)) => {
            cross_boundary_success_return(result, format!("{:?}", block.header.hash()))
        }
        Ok(None) => cross_boundary_error_return(result, "Invalid block number"),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
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
pub fn evm_try_get_block_number_by_hash(result: &mut ffi::CrossBoundaryResult, hash: &str) -> u64 {
    let Ok(hash) = hash.parse() else {
        return cross_boundary_error_return(result, "Invalid block hash");
    };

    match SERVICES.evm.storage.get_block_by_hash(&hash) {
        Ok(Some(block)) => {
            let Ok(block_number) = u64::try_from(block.header.number) else {
                return cross_boundary_error_return(result, "Block number value overflow");
            };
            cross_boundary_success_return(result, block_number)
        }
        Ok(None) => cross_boundary_error_return(result, "Invalid block hash"),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn evm_try_get_block_header_by_hash(
    result: &mut ffi::CrossBoundaryResult,
    hash: &str,
) -> ffi::EVMBlockHeader {
    let Ok(hash) = hash.parse() else {
        return cross_boundary_error_return(result, "Invalid block hash");
    };

    match SERVICES.evm.storage.get_block_by_hash(&hash) {
        Ok(Some(block)) => {
            let Ok(number) = u64::try_from(block.header.number) else {
                return cross_boundary_error_return(result, "block number value overflow");
            };
            let Ok(gas_limit) = u64::try_from(block.header.gas_limit) else {
                return cross_boundary_error_return(result, "block gas limit value overflow");
            };
            let Ok(gas_used) = u64::try_from(block.header.gas_used) else {
                return cross_boundary_error_return(result, "block gas used value overflow");
            };
            let Ok(base_fee) = u64::try_from(WeiAmount(block.header.base_fee).to_satoshi()) else {
                return cross_boundary_error_return(result, "base fee value overflow");
            };

            let out = ffi::EVMBlockHeader {
                parent_hash: format!("{:?}", block.header.parent_hash),
                beneficiary: format!("{:?}", block.header.beneficiary),
                state_root: format!("{:?}", block.header.state_root),
                receipts_root: format!("{:?}", block.header.receipts_root),
                number,
                gas_limit,
                gas_used,
                timestamp: block.header.timestamp,
                extra_data: block.header.extra_data.clone(),
                mix_hash: format!("{:?}", block.header.mix_hash),
                nonce: block.header.nonce.to_low_u64_be(),
                base_fee,
            };
            cross_boundary_success_return(result, out)
        }
        Ok(None) => cross_boundary_error_return(result, "Invalid block hash"),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn evm_try_get_block_count(result: &mut ffi::CrossBoundaryResult) -> u64 {
    match SERVICES.evm.block.get_latest_block_hash_and_number() {
        Ok(Some((_, number))) => {
            let Ok(number) = u64::try_from(number) else {
                return cross_boundary_error_return(result, "Count value overflow");
            };
            cross_boundary_success_return(result, number)
        }
        Ok(None) => cross_boundary_error_return(result, "Unable to get block count"),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn evm_try_is_dst20_deployed_or_queued(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    name: &str,
    symbol: &str,
    token_id: u64,
) -> bool {
    unsafe {
        match SERVICES
            .evm
            .is_dst20_deployed_or_queued(queue_id, name, symbol, token_id)
        {
            Ok(is_deployed) => cross_boundary_success_return(result, is_deployed),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

pub fn evm_try_get_tx_by_hash(
    result: &mut ffi::CrossBoundaryResult,
    tx_hash: &str,
) -> ffi::EVMTransaction {
    let Ok(tx_hash) = tx_hash.parse() else {
        return cross_boundary_error_return(result, "Invalid tx hash");
    };

    match SERVICES.evm.storage.get_transaction_by_hash(&tx_hash) {
        Ok(Some(tx)) => {
            let Ok(tx) = SignedTx::try_from(tx) else {
                return cross_boundary_error_return(result, "failed to convert tx to SignedTx");
            };

            let Ok(nonce) = u64::try_from(tx.nonce()) else {
                return cross_boundary_error_return(result, "tx nonce value overflow");
            };

            let Ok(gas_limit) = u64::try_from(tx.gas_limit()) else {
                return cross_boundary_error_return(result, "tx gas limit value overflow");
            };

            let Ok(value) = u64::try_from(WeiAmount(tx.value()).to_satoshi()) else {
                return cross_boundary_error_return(result, "tx value overflow");
            };

            let mut tx_type = 0u8;
            let mut gas_price = 0u64;
            let mut max_fee_per_gas = 0u64;
            let mut max_priority_fee_per_gas = 0u64;
            match &tx.transaction {
                TransactionV2::Legacy(transaction) => {
                    let Ok(price) = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi())
                    else {
                        return cross_boundary_error_return(result, "tx gas price value overflow");
                    };
                    gas_price = price;
                }
                TransactionV2::EIP2930(transaction) => {
                    tx_type = 1u8;
                    let Ok(price) = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi())
                    else {
                        return cross_boundary_error_return(result, "tx gas price value overflow");
                    };
                    gas_price = price;
                }
                TransactionV2::EIP1559(transaction) => {
                    tx_type = 2u8;
                    let Ok(price) =
                        u64::try_from(WeiAmount(transaction.max_fee_per_gas).to_satoshi())
                    else {
                        return cross_boundary_error_return(
                            result,
                            "tx max fee per gas value overflow",
                        );
                    };
                    max_fee_per_gas = price;
                    let Ok(price) =
                        u64::try_from(WeiAmount(transaction.max_priority_fee_per_gas).to_satoshi())
                    else {
                        return cross_boundary_error_return(
                            result,
                            "tx max priority fee per gas value overflow",
                        );
                    };
                    max_priority_fee_per_gas = price;
                }
            }

            let out = ffi::EVMTransaction {
                tx_type,
                hash: format!("{:?}", tx.hash()),
                sender: format!("{:?}", tx.sender),
                nonce,
                gas_price,
                gas_limit,
                max_fee_per_gas,
                max_priority_fee_per_gas,
                create_tx: match tx.action() {
                    TransactionAction::Call(_) => false,
                    TransactionAction::Create => true,
                },
                to: match tx.to() {
                    Some(to) => format!("{:?}", to),
                    None => String::new(),
                },
                value,
                data: tx.data().to_vec(),
            };
            cross_boundary_success_return(result, out)
        }
        Ok(None) => cross_boundary_error_return(result, "Unable to get evm tx from tx hash"),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn evm_try_create_dst20(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
    native_hash: &str,
    name: &str,
    symbol: &str,
    token_id: u64,
) {
    let native_hash = String::from(native_hash);
    let address = match ain_contracts::dst20_address_from_token_id(token_id) {
        Ok(address) => address,
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    };
    debug!("Deploying to address {:#?}", address);

    let system_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        name: String::from(name),
        symbol: String::from(symbol),
        address,
        token_id,
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
    amount: u64,
    native_hash: &str,
    token_id: u64,
    out: bool,
) {
    let Ok(address) = address.parse() else {
        return cross_boundary_error_return(result, "Invalid address");
    };
    let amount = match try_from_satoshi(U256::from(amount)) {
        Ok(wei_amount) => wei_amount,
        Err(e) => return cross_boundary_error_return(result, e.to_string()),
    };
    let native_hash = String::from(native_hash);
    let contract = ain_contracts::dst20_address_from_token_id(token_id)
        .unwrap_or_else(|e| cross_boundary_error_return(result, e.to_string()));

    let system_tx = QueueTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
        to: address,
        contract,
        amount: amount.0,
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

/// Retrieves the queue target block
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
///
/// # Returns
///
/// Returns the target block for a specific queue_id as a `u64`
pub fn evm_unsafe_try_get_target_block_in_q(
    result: &mut ffi::CrossBoundaryResult,
    queue_id: u64,
) -> u64 {
    unsafe {
        match SERVICES.evm.core.get_target_block_in(queue_id) {
            Ok(target_block) => cross_boundary_success_return(result, target_block.as_u64()),
            Err(e) => cross_boundary_error_return(result, e.to_string()),
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_hash_type_string() {
        use primitive_types::H160;
        let num = 0b11010111_11010111_11010111_11010111_11010111_11010111_11010111_11010111;
        let num_h160 = H160::from_low_u64_be(num);
        let num_h160_string = format!("{:?}", num_h160);
        println!("{}", num_h160_string);

        let num_h160_test: H160 = num_h160_string.parse().unwrap();
        assert_eq!(num_h160_test, num_h160);

        use primitive_types::H256;
        let num_h256: H256 = "0x3186715414c5fbd73586662d26b83b66b5754036379d56e896a560a90e409351"
            .parse()
            .unwrap();
        let num_h256_string = format!("{:?}", num_h256);
        println!("{}", num_h256_string);
        let num_h256_test: H256 = num_h256_string.parse().unwrap();
        assert_eq!(num_h256_test, num_h256);
    }
}
