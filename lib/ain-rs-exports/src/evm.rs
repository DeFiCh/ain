use ain_contracts::{
    get_transfer_domain_contract, get_transferdomain_dst20_transfer_function,
    get_transferdomain_native_transfer_function, FixedContract,
};
use ain_evm::{
    core::{TransferDomainTxInfo, XHash},
    evm::FinalizedBlockInfo,
    fee::calculate_max_tip_gas_fee,
    services::SERVICES,
    storage::traits::{BlockStorage, Rollback, TransactionStorage},
    transaction::{
        self,
        system::{DST20Data, DeployContractData, SystemTx, TransferDirection, TransferDomainData},
    },
    txqueue::QueueTx,
    weiamount::{try_from_gwei, try_from_satoshi, WeiAmount},
    Result,
};
use ain_macros::ffi_fallible;
use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature, TransactionV2};
use ethereum_types::{H160, H256, U256};
use log::debug;
use transaction::{LegacyUnsignedTransaction, LOWER_H256};

use crate::{
    ffi::{self, TxInfo},
    prelude::*,
};

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
#[ffi_fallible]
fn create_and_sign_tx(ctx: ffi::CreateTransactionContext) -> Result<ffi::CreateTxResult> {
    let to_action = if ctx.to.is_empty() {
        TransactionAction::Create
    } else {
        let to_address = ctx.to.parse::<H160>().map_err(|_| "Invalid address")?;
        TransactionAction::Call(to_address)
    };
    let nonce = U256::from(ctx.nonce);
    let gas_price = try_from_gwei(U256::from(ctx.gas_price))?;
    let gas_limit = U256::from(ctx.gas_limit);
    let value = try_from_satoshi(U256::from(ctx.value))?;

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
    let signed = t.sign(&ctx.priv_key, ctx.chain_id)?;
    let nonce = u64::try_from(signed.nonce)?;
    Ok(ffi::CreateTxResult {
        tx: signed.encode().into(),
        nonce,
    })
}

/// Creates and signs a transfer domain transaction.
///
/// # Arguments
///
/// * `to` - The address to transfer funds to.
/// * `direction` - True if sending to EVM. False if sending from EVM
/// * `value` - Amount to send
/// * `priv_key` - Key used to sign the TX
///
/// # Errors
///
/// Returns a `TransactionError` if signing fails.
///
/// # Returns
///
/// Returns the signed transaction encoded as a byte vector on success.
#[ffi_fallible]
fn create_and_sign_transfer_domain_tx(
    ctx: ffi::CreateTransferDomainContext,
) -> Result<ffi::CreateTxResult> {
    let FixedContract { fixed_address, .. } = get_transfer_domain_contract();
    let action = TransactionAction::Call(fixed_address);

    let sender = ctx.from.parse::<H160>().map_err(|_| "Invalid address")?;

    let (from_address, to_address) = if ctx.direction {
        let to_address = ctx.to.parse::<H160>().map_err(|_| "Invalid address")?;
        // Send EvmIn from contract address
        (fixed_address, to_address)
    } else {
        let from_address = ctx.from.parse::<H160>().map_err(|_| "Invalid address")?;
        // Send EvmOut to contract address
        (from_address, fixed_address)
    };

    let value = try_from_satoshi(U256::from(ctx.value))?;

    let input = {
        let from_address = ethabi::Token::Address(from_address);
        let to_address = ethabi::Token::Address(to_address);
        let value = ethabi::Token::Uint(value.0);
        let native_address = ethabi::Token::String(ctx.native_address);

        let is_native_token_transfer = ctx.token_id == 0;
        if is_native_token_transfer {
            let function = get_transferdomain_native_transfer_function();
            function.encode_input(&[from_address, to_address, value, native_address])
        } else {
            let contract_address = {
                let address = ain_contracts::dst20_address_from_token_id(u64::from(ctx.token_id))?;
                ethabi::Token::Address(address)
            };
            let function = get_transferdomain_dst20_transfer_function();
            function.encode_input(&[
                contract_address,
                from_address,
                to_address,
                value,
                native_address,
            ])
        }
    }?;

    let state_root = SERVICES.evm.core.get_state_root()?;
    let nonce = if ctx.use_nonce {
        U256::from(ctx.nonce)
    } else {
        SERVICES
            .evm
            .core
            .get_next_account_nonce(sender, state_root)?
    };

    let t = LegacyUnsignedTransaction {
        nonce,
        gas_price: U256::zero(),
        gas_limit: U256::zero(),
        action,
        value: U256::zero(),
        input,
        sig: TransactionSignature::new(27, LOWER_H256, LOWER_H256).unwrap(),
    };

    let signed = t.sign(&ctx.priv_key, ctx.chain_id)?;
    let nonce = u64::try_from(signed.nonce)?;
    Ok(ffi::CreateTxResult {
        tx: signed.encode().into(),
        nonce,
    })
}

#[ffi_fallible]
fn store_account_nonce(from_address: &str, nonce: u64) -> Result<()> {
    let from_address = from_address
        .parse::<H160>()
        .map_err(|_| "Invalid address")?;
    let _ = SERVICES
        .evm
        .core
        .store_account_nonce(from_address, U256::from(nonce));
    Ok(())
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
#[ffi_fallible]
fn get_balance(address: &str) -> Result<u64> {
    let address = address.parse::<H160>().map_err(|_| "Invalid address")?;
    let (_, latest_block_number) = SERVICES
        .evm
        .block
        .get_latest_block_hash_and_number()?
        .unwrap_or_default();

    let balance = SERVICES
        .evm
        .core
        .get_balance(address, latest_block_number)?;
    let amount = WeiAmount(balance).to_satoshi().try_into()?;

    Ok(amount)
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
#[ffi_fallible]
fn unsafe_get_next_valid_nonce_in_q(queue_id: u64, address: &str) -> Result<u64> {
    let address = address.parse::<H160>().map_err(|_| "Invalid address")?;

    unsafe {
        let next_nonce = SERVICES
            .evm
            .core
            .get_next_valid_nonce_in_queue(queue_id, address)?;

        let nonce = u64::try_from(next_nonce)?;
        Ok(nonce)
    }
}

/// Removes all transactions in the queue above a native hash
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `address` - The EVM address of the account.
#[ffi_fallible]
fn unsafe_remove_txs_above_hash_in_q(queue_id: u64, target_hash: String) -> Result<Vec<String>> {
    unsafe {
        SERVICES
            .evm
            .core
            .remove_txs_above_hash_in(queue_id, target_hash)
    }
}

/// `EvmIn`. Send DFI to an EVM account.
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
/// * `address` - The EVM address of the account.
/// * `amount` - The amount to add as a byte array.
/// * `hash` - The hash value as a byte array.
#[ffi_fallible]
fn unsafe_add_balance_in_q(queue_id: u64, raw_tx: &str, native_hash: &str) -> Result<()> {
    let signed_tx = SERVICES
        .evm
        .core
        .signed_tx_cache
        .try_get_or_create(raw_tx)?;
    let native_hash = XHash::from(native_hash);

    let queue_tx = QueueTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
        signed_tx: Box::new(signed_tx),
        direction: TransferDirection::EvmIn,
    }));

    unsafe {
        SERVICES
            .evm
            .push_tx_in_queue(queue_id, queue_tx, native_hash)
    }
}

/// `EvmOut`. Send DFI from an EVM account.
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
/// - the `queue_id` does not match any existing queue
/// - the address is not a valid EVM address
/// - the account has insufficient balance.
///
/// # Returns
///
/// Returns `true` if the balance subtraction is successful, `false` otherwise.
#[ffi_fallible]
fn unsafe_sub_balance_in_q(queue_id: u64, raw_tx: &str, native_hash: &str) -> Result<bool> {
    let signed_tx = SERVICES
        .evm
        .core
        .signed_tx_cache
        .try_get_or_create(raw_tx)?;
    let native_hash = XHash::from(native_hash);

    let queue_tx = QueueTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
        signed_tx: Box::new(signed_tx),
        direction: TransferDirection::EvmOut,
    }));

    unsafe {
        SERVICES
            .evm
            .push_tx_in_queue(queue_id, queue_tx, native_hash)?;
        Ok(true)
    }
}

/// Validates a raw EVM transaction.
///
/// # Arguments
///
/// * `result` - Result object
/// * `queue_id` - The EVM queue ID
/// * `tx` - The raw transaction string.
///
/// # Errors
///
/// Returns an Error if:
/// - The hex data is invalid
/// - The EVM transaction is invalid
/// - The EVM transaction fee is lower than the initial block base fee
/// - The EVM transaction values exceed money range.
/// - Could not fetch the underlying EVM account
/// - Account's nonce is more than raw tx's nonce
/// - The EVM transaction max prepay gas is invalid
/// - The EVM transaction gas limit is lower than the transaction intrinsic gas
///
/// # Returns
///
/// Returns the validation result.
#[ffi_fallible]
fn unsafe_validate_raw_tx_in_q(queue_id: u64, raw_tx: &str) -> Result<()> {
    debug!("[unsafe_validate_raw_tx_in_q]");
    unsafe {
        let _ = SERVICES.evm.core.validate_raw_tx(raw_tx, queue_id)?;
        Ok(())
    }
}

/// Validates a raw transfer domain EVM transaction.
///
/// # Arguments
///
/// * `result` - Result object
/// * `queue_id` - The EVM queue ID
/// * `tx` - The raw transaction string.
///
/// # Errors
///
/// Returns an Error if:
/// - The hex data is invalid
/// - The EVM transaction is invalid
/// - Could not fetch the underlying EVM account
/// - Account's nonce does not match raw tx's nonce
/// - The EVM transaction value is not zero
/// - The EVM tranasction action is not a call to the transferdomain contract address
/// - The EVM transaction execution is unsuccessful
///
/// # Returns
///
/// Returns the validation result.
#[ffi_fallible]
fn unsafe_validate_transferdomain_tx_in_q(
    queue_id: u64,
    raw_tx: &str,
    context: ffi::TransferDomainInfo,
) -> Result<()> {
    debug!("[unsafe_validate_transferdomain_tx_in_q]");
    unsafe {
        let _ = SERVICES.evm.core.validate_raw_transferdomain_tx(
            raw_tx,
            queue_id,
            TransferDomainTxInfo {
                from: context.from,
                to: context.to,
                native_address: context.native_address,
                direction: context.direction,
                value: context.value,
                token_id: context.token_id,
            },
        )?;
        Ok(())
    }
}

/// Retrieves the EVM queue ID.
///
/// # Returns
///
/// Returns the EVM queue ID as a `u64`.
#[ffi_fallible]
fn unsafe_create_queue(timestamp: u64) -> Result<u64> {
    unsafe { SERVICES.evm.core.create_queue(timestamp) }
}

/// /// Discards an EVM queue.
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
///
#[ffi_fallible]
fn unsafe_remove_queue(queue_id: u64) -> Result<()> {
    unsafe { SERVICES.evm.core.remove_queue(queue_id) }
    Ok(())
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
#[ffi_fallible]
fn unsafe_push_tx_in_q(
    queue_id: u64,
    raw_tx: &str,
    native_hash: &str,
) -> Result<ffi::ValidateTxCompletion> {
    let native_hash = native_hash.to_string();

    unsafe {
        let signed_tx = SERVICES
            .evm
            .core
            .signed_tx_cache
            .try_get_or_create(raw_tx)?;

        let tx_hash = signed_tx.hash();
        SERVICES
            .evm
            .push_tx_in_queue(queue_id, signed_tx.into(), native_hash)?;

        Ok(ffi::ValidateTxCompletion {
            tx_hash: format!("{:?}", tx_hash),
        })
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
#[ffi_fallible]
fn unsafe_construct_block_in_q(
    queue_id: u64,
    difficulty: u32,
    miner_address: &str,
    timestamp: u64,
    dvm_block_number: u64,
    mnview_ptr: usize,
) -> Result<ffi::FinalizeBlockCompletion> {
    let eth_address = miner_address
        .parse::<H160>()
        .map_err(|_| "Invalid address")?;

    unsafe {
        let FinalizedBlockInfo {
            block_hash,
            failed_transactions,
            total_burnt_fees,
            total_priority_fees,
            block_number,
        } = SERVICES.evm.construct_block_in_queue(
            queue_id,
            difficulty,
            eth_address,
            timestamp,
            dvm_block_number,
            mnview_ptr,
        )?;
        let total_burnt_fees = u64::try_from(WeiAmount(total_burnt_fees).to_satoshi())?;
        let total_priority_fees = u64::try_from(WeiAmount(total_priority_fees).to_satoshi())?;

        Ok(ffi::FinalizeBlockCompletion {
            block_hash,
            failed_transactions,
            total_burnt_fees,
            total_priority_fees,
            block_number: block_number.as_u64(),
        })
    }
}

#[ffi_fallible]
fn unsafe_commit_queue(queue_id: u64) -> Result<()> {
    unsafe { SERVICES.evm.commit_queue(queue_id) }
}

#[ffi_fallible]
fn disconnect_latest_block() -> Result<()> {
    SERVICES.evm.core.clear_account_nonce();
    SERVICES.evm.storage.disconnect_latest_block()
}

#[ffi_fallible]
fn handle_attribute_apply(
    _queue_id: u64,
    _attribute_type: ffi::GovVarKeyDataStructure,
    _value: Vec<u8>,
) -> Result<bool> {
    Ok(true)
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
#[ffi_fallible]
fn get_block_hash_by_number(height: u64) -> Result<XHash> {
    let block = SERVICES
        .evm
        .storage
        .get_block_by_number(&U256::from(height))?
        .ok_or("Invalid block number")?;
    Ok(format!("{:?}", block.hash))
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
#[ffi_fallible]
fn get_block_number_by_hash(hash: &str) -> Result<u64> {
    let hash = hash.parse::<H256>().map_err(|_| "Invalid hash")?;

    let block = SERVICES
        .evm
        .storage
        .get_block_by_hash(&hash)?
        .ok_or("Invalid block hash")?;
    let block_number = u64::try_from(block.header.number)?;
    Ok(block_number)
}

#[ffi_fallible]
fn get_block_header_by_hash(hash: &str) -> Result<ffi::EVMBlockHeader> {
    let hash = hash.parse::<H256>().map_err(|_| "Invalid hash")?;

    let block = SERVICES
        .evm
        .storage
        .get_block_by_hash(&hash)?
        .ok_or("Invalid block hash")?;

    let number = u64::try_from(block.header.number)?;
    let gas_limit = u64::try_from(block.header.gas_limit)?;
    let gas_used = u64::try_from(block.header.gas_used)?;
    let base_fee = u64::try_from(WeiAmount(block.header.base_fee).to_satoshi())?;

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
    Ok(out)
}

#[ffi_fallible]
fn get_block_count() -> Result<u64> {
    let (_, block_number) = SERVICES
        .evm
        .block
        .get_latest_block_hash_and_number()?
        .ok_or("Unable to get block block_number")?;
    let count = u64::try_from(block_number)?;
    Ok(count)
}

#[ffi_fallible]
fn is_dst20_deployed_or_queued(
    queue_id: u64,
    name: &str,
    symbol: &str,
    token_id: u64,
) -> Result<bool> {
    unsafe {
        SERVICES
            .evm
            .is_dst20_deployed_or_queued(queue_id, name, symbol, token_id)
    }
}

#[ffi_fallible]
fn get_tx_by_hash(tx_hash: &str) -> Result<ffi::EVMTransaction> {
    let tx_hash = tx_hash.parse::<H256>().map_err(|_| "Invalid hash")?;

    let tx = SERVICES
        .evm
        .storage
        .get_transaction_by_hash(&tx_hash)?
        .ok_or("Unable to get evm tx from tx hash")?;

    let tx = SERVICES
        .evm
        .core
        .signed_tx_cache
        .try_get_or_create_from_tx(&tx)?;

    let nonce = u64::try_from(tx.nonce())?;
    let gas_limit = u64::try_from(tx.gas_limit())?;
    let value = u64::try_from(WeiAmount(tx.value()).to_satoshi())?;

    let mut tx_type = 0u8;
    let mut gas_price = 0u64;
    let mut max_fee_per_gas = 0u64;
    let mut max_priority_fee_per_gas = 0u64;
    match &tx.transaction {
        TransactionV2::Legacy(transaction) => {
            let price = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi())?;
            gas_price = price;
        }
        TransactionV2::EIP2930(transaction) => {
            tx_type = 1u8;
            let price = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi())?;
            gas_price = price;
        }
        TransactionV2::EIP1559(transaction) => {
            tx_type = 2u8;
            let price = u64::try_from(WeiAmount(transaction.max_fee_per_gas).to_satoshi())?;
            max_fee_per_gas = price;
            let price =
                u64::try_from(WeiAmount(transaction.max_priority_fee_per_gas).to_satoshi())?;
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
            Some(to) => format!("{to:?}"),
            None => XHash::new(),
        },
        value,
        data: tx.data().to_vec(),
    };
    Ok(out)
}

#[ffi_fallible]
fn parse_tx_from_raw(raw_tx: &str) -> Result<ffi::EVMTransaction> {
    let tx = SERVICES
        .evm
        .core
        .signed_tx_cache
        .try_get_or_create(raw_tx)?;
    let nonce = u64::try_from(tx.nonce())?;
    let gas_limit = u64::try_from(tx.gas_limit())?;
    let value = u64::try_from(WeiAmount(tx.value()).to_satoshi())?;

    let mut tx_type = 0u8;
    let mut gas_price = 0u64;
    let mut max_fee_per_gas = 0u64;
    let mut max_priority_fee_per_gas = 0u64;
    match &tx.transaction {
        TransactionV2::Legacy(transaction) => {
            let price = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi())?;
            gas_price = price;
        }
        TransactionV2::EIP2930(transaction) => {
            tx_type = 1u8;
            let price = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi())?;
            gas_price = price;
        }
        TransactionV2::EIP1559(transaction) => {
            tx_type = 2u8;
            let price = u64::try_from(WeiAmount(transaction.max_fee_per_gas).to_satoshi())?;
            max_fee_per_gas = price;
            let price =
                u64::try_from(WeiAmount(transaction.max_priority_fee_per_gas).to_satoshi())?;
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
            Some(to) => format!("{to:?}"),
            None => XHash::new(),
        },
        value,
        data: tx.data().to_vec(),
    };
    Ok(out)
}

#[ffi_fallible]
fn create_dst20(
    queue_id: u64,
    native_hash: &str,
    name: &str,
    symbol: &str,
    token_id: u64,
) -> Result<()> {
    let native_hash = XHash::from(native_hash);
    let address = ain_contracts::dst20_address_from_token_id(token_id)?;
    debug!("Deploying to address {:#?}", address);

    let system_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        name: String::from(name),
        symbol: String::from(symbol),
        address,
        token_id,
    }));

    unsafe {
        SERVICES
            .evm
            .push_tx_in_queue(queue_id, system_tx, native_hash)
    }
}

#[ffi_fallible]
fn unsafe_bridge_dst20(
    queue_id: u64,
    raw_tx: &str,
    native_hash: &str,
    token_id: u64,
    out: bool,
) -> Result<()> {
    let native_hash = XHash::from(native_hash);
    let contract_address = ain_contracts::dst20_address_from_token_id(token_id)?;
    let signed_tx = SERVICES
        .evm
        .core
        .signed_tx_cache
        .try_get_or_create(raw_tx)?;
    let system_tx = QueueTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
        signed_tx: Box::new(signed_tx),
        contract_address,
        direction: out.into(),
    }));

    unsafe {
        SERVICES
            .evm
            .push_tx_in_queue(queue_id, system_tx, native_hash)
    }
}

/// Retrieves a raw tx's transaction hash
/// # Arguments
///
/// * `raw_tx` - The transaction as raw hex
///
/// # Returns
///
/// Returns the transaction's hash
#[ffi_fallible]
fn get_tx_hash(raw_tx: &str) -> Result<String> {
    let signed_tx = SERVICES
        .evm
        .core
        .signed_tx_cache
        .try_get_or_create(raw_tx)?;
    Ok(format!("{:?}", signed_tx.hash()))
}

/// Retrieves the queue target block
///
/// # Arguments
///
/// * `queue_id` - The queue ID.
///
/// # Returns
///
/// Returns the target block for a specific `queue_id` as a `u64`
#[ffi_fallible]
fn unsafe_get_target_block_in_q(queue_id: u64) -> Result<u64> {
    let target_block = unsafe { SERVICES.evm.core.get_target_block_in(queue_id)? };
    Ok(target_block.as_u64())
}

/// Checks if the given address is a smart contract
///
/// # Arguments
///
/// * `address` - The address to check.
///
/// # Returns
///
/// Returns `true` if the address is a contract, `false` otherwise
#[ffi_fallible]
fn unsafe_is_smart_contract_in_q(address: &str, queue_id: u64) -> Result<bool> {
    let address = address.parse::<H160>().map_err(|_| "Invalid address")?;

    unsafe { SERVICES.evm.is_smart_contract_in_queue(address, queue_id) }
}

#[ffi_fallible]
fn get_tx_info_from_raw_tx(raw_tx: &str) -> Result<TxInfo> {
    let signed_tx = SERVICES
        .evm
        .core
        .signed_tx_cache
        .try_get_or_create(raw_tx)?;

    let nonce = u64::try_from(signed_tx.nonce())?;
    let initial_base_fee = SERVICES.evm.block.calculate_base_fee(H256::zero())?;
    let tip_fee = calculate_max_tip_gas_fee(&signed_tx, initial_base_fee)?;
    let tip_fee = u64::try_from(tip_fee)?;

    Ok(TxInfo {
        nonce,
        address: format!("{:?}", signed_tx.sender),
        tip_fee,
    })
}

#[ffi_fallible]
fn unsafe_get_total_gas_used(queue_id: u64) -> Result<String> {
    unsafe { Ok(SERVICES.evm.core.get_total_gas_used(queue_id)) }
}

#[ffi_fallible]
fn get_block_limit() -> Result<u64> {
    SERVICES.evm.get_block_limit()
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_hash_type_string() {
        use ethereum_types::H160;
        let num = 0b11010111_11010111_11010111_11010111_11010111_11010111_11010111_11010111;
        let num_h160 = H160::from_low_u64_be(num);
        let num_h160_string = format!("{:?}", num_h160);
        println!("{}", num_h160_string);

        let num_h160_test: H160 = num_h160_string.parse().unwrap();
        assert_eq!(num_h160_test, num_h160);

        use ethereum_types::H256;
        let num_h256: H256 = "0x3186715414c5fbd73586662d26b83b66b5754036379d56e896a560a90e409351"
            .parse()
            .unwrap();
        let num_h256_string = format!("{:?}", num_h256);
        println!("{}", num_h256_string);
        let num_h256_test: H256 = num_h256_string.parse().unwrap();
        assert_eq!(num_h256_test, num_h256);
    }
}
