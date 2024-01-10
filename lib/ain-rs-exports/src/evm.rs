use ain_contracts::{
    get_transfer_domain_contract, get_transferdomain_dst20_transfer_function,
    get_transferdomain_native_transfer_function, FixedContract,
};
use ain_evm::{
    core::{TransferDomainTxInfo, XHash},
    evm::FinalizedBlockInfo,
    executor::ExecuteTx,
    fee::{calculate_max_tip_gas_fee, calculate_min_rbf_tip_gas_fee},
    log::Notification,
    services::SERVICES,
    storage::traits::{BlockStorage, Rollback, TransactionStorage},
    transaction::{
        self,
        system::{DST20Data, DeployContractData, SystemTx, TransferDirection, TransferDomainData},
        SignedTx,
    },
    weiamount::{try_from_gwei, try_from_satoshi, WeiAmount},
    Result,
};
use ain_macros::ffi_fallible;
use anyhow::format_err;
use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature, TransactionV2};
use ethereum_types::{H160, H256, U256};
use log::debug;
use transaction::{LegacyUnsignedTransaction, LOWER_H256};

use crate::{
    ffi::{self, CrossBoundaryResult, TxMinerInfo},
    prelude::*,
    BlockTemplateWrapper,
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
fn evm_try_create_and_sign_tx(ctx: ffi::CreateTransactionContext) -> Result<ffi::CreateTxResult> {
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
/// * `ctx` - The transferdomain transaction context.
///
/// # Errors
///
/// Returns a `TransactionError` if signing fails.
///
/// # Returns
///
/// Returns the signed transaction encoded as a byte vector on success.
#[ffi_fallible]
fn evm_try_create_and_sign_transfer_domain_tx(
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

    let state_root = SERVICES.evm.core.get_latest_state_root()?;
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
fn evm_try_store_account_nonce(from_address: &str, nonce: u64) -> Result<()> {
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
fn evm_try_get_balance(address: &str) -> Result<u64> {
    let address = address.parse::<H160>().map_err(|_| "Invalid address")?;
    let state_root = SERVICES.evm.block.get_latest_state_root()?;

    let balance = SERVICES.evm.core.get_balance(address, state_root)?;
    let amount = WeiAmount(balance).to_satoshi()?.try_into()?;

    Ok(amount)
}

/// Updates the block template in a specific template
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
/// * `mnview_ptr` - The pointer to the DVM accounts view.
///
/// # Returns
///
/// The state update results.
#[ffi_fallible]
fn evm_try_unsafe_update_state_in_template(template: &mut BlockTemplateWrapper) -> Result<()> {
    unsafe {
        SERVICES
            .evm
            .update_state_in_block_template(template.get_inner_mut()?)
    }
}

/// Retrieves the next valid nonce of an EVM account in a specific template
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
/// * `address` - The EVM address of the account.
///
/// # Returns
///
/// Returns the next valid nonce of the account in a specific template
#[ffi_fallible]
fn evm_try_unsafe_get_next_valid_nonce_in_template(
    template: &BlockTemplateWrapper,
    address: &str,
) -> Result<u64> {
    let address = address.parse::<H160>().map_err(|_| "Invalid address")?;

    unsafe {
        let next_nonce = SERVICES
            .evm
            .core
            .get_next_valid_nonce_in_block_template(template.get_inner()?, address)?;

        let nonce = u64::try_from(next_nonce)?;
        Ok(nonce)
    }
}

/// Removes all transactions in the block template above a native hash
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
/// * `target_hash` - The native hash of the tx to be targeted and removed.
#[ffi_fallible]
fn evm_try_unsafe_remove_txs_above_hash_in_template(
    template: &mut BlockTemplateWrapper,
    target_hash: String,
) -> Result<Vec<String>> {
    unsafe {
        SERVICES
            .evm
            .core
            .remove_txs_above_hash_in_block_template(template.get_inner_mut()?, target_hash)
    }
}

/// `EvmIn`. Send DFI to an EVM account.
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
/// * `raw_tx` - The raw transparent transferdomain tx.
/// * `hash` - The native hash of the transferdomain tx.
#[ffi_fallible]
fn evm_try_unsafe_add_balance_in_template(
    template: &mut BlockTemplateWrapper,
    raw_tx: &str,
    native_hash: &str,
) -> Result<()> {
    let signed_tx = SERVICES.evm.core.tx_cache.try_get_or_create(raw_tx)?;
    let native_hash = XHash::from(native_hash);

    let exec_tx = ExecuteTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
        signed_tx: Box::new(signed_tx),
        direction: TransferDirection::EvmIn,
    }));
    unsafe {
        SERVICES
            .evm
            .push_tx_in_block_template(template.get_inner_mut()?, exec_tx, native_hash)
    }
}

/// `EvmOut`. Send DFI from an EVM account.
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
/// * `raw_tx` - The raw transparent transferdomain tx.
/// * `hash` - The native hash of the transferdomain tx.
#[ffi_fallible]
fn evm_try_unsafe_sub_balance_in_template(
    template: &mut BlockTemplateWrapper,
    raw_tx: &str,
    native_hash: &str,
) -> Result<bool> {
    let signed_tx = SERVICES.evm.core.tx_cache.try_get_or_create(raw_tx)?;
    let native_hash = XHash::from(native_hash);

    let exec_tx = ExecuteTx::SystemTx(SystemTx::TransferDomain(TransferDomainData {
        signed_tx: Box::new(signed_tx),
        direction: TransferDirection::EvmOut,
    }));

    unsafe {
        SERVICES
            .evm
            .push_tx_in_block_template(template.get_inner_mut()?, exec_tx, native_hash)?;
        Ok(true)
    }
}

/// Validates a raw EVM transaction.
///
/// # Arguments
///
/// * `result` - Result object
/// * `template` - The EVM BlockTemplate
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
fn evm_try_unsafe_validate_raw_tx_in_template(
    template: &BlockTemplateWrapper,
    raw_tx: &str,
) -> Result<()> {
    debug!("[unsafe_validate_raw_tx_in_template]");
    unsafe {
        let _ = SERVICES
            .evm
            .core
            .validate_raw_tx(raw_tx, template.get_inner()?)?;
        Ok(())
    }
}

/// Validates a raw transfer domain EVM transaction.
///
/// # Arguments
///
/// * `result` - Result object
/// * `template` - The EVM BlockTemplate
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
fn evm_try_unsafe_validate_transferdomain_tx_in_template(
    template: &BlockTemplateWrapper,
    raw_tx: &str,
    context: ffi::TransferDomainInfo,
) -> Result<()> {
    debug!("[unsafe_validate_transferdomain_tx_in_template]");
    unsafe {
        let _ = SERVICES.evm.core.validate_raw_transferdomain_tx(
            raw_tx,
            template.get_inner()?,
            TransferDomainTxInfo {
                from: context.from,
                to: context.to,
                native_address: context.native_address,
                direction: context.direction,
                value: context.value,
                token_id: context.token_id,
            },
        )?;
    }
    Ok(())
}

fn block_template_err_wrapper() -> &'static mut BlockTemplateWrapper {
    // We don't really care if multiple thread reinitialize or use it as long as the refs live
    // So we just use unsafe mut pattern since it's purely for err condition that is intented
    // to never be used
    static mut CELL: std::cell::OnceCell<BlockTemplateWrapper> = std::cell::OnceCell::new();
    unsafe {
        let v = CELL.get_or_init(|| BlockTemplateWrapper(None));
        #[allow(mutable_transmutes)]
        std::mem::transmute(v)
    }
}

/// Creates an EVM block template.
///
/// # Returns
///
/// Returns the EVM template.
pub fn evm_try_unsafe_create_template(
    result: &mut CrossBoundaryResult,
    dvm_block: u64,
    miner_address: &str,
    difficulty: u32,
    timestamp: u64,
    mnview_ptr: usize,
) -> &'static mut BlockTemplateWrapper {
    let miner_address = if miner_address.is_empty() {
        H160::zero()
    } else {
        match miner_address.parse::<H160>() {
            Ok(a) => a,
            Err(_) => {
                cross_boundary_error(result, "Invalid address");
                return block_template_err_wrapper();
            }
        }
    };

    unsafe {
        match SERVICES.evm.create_block_template(
            dvm_block,
            miner_address,
            difficulty,
            timestamp,
            mnview_ptr,
        ) {
            Ok(template) => cross_boundary_success_return(
                result,
                Box::leak(Box::new(BlockTemplateWrapper(Some(template)))),
            ),
            Err(e) => {
                cross_boundary_error(result, e.to_string());
                block_template_err_wrapper()
            }
        }
    }
}

/// /// Discards an EVM block template.
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
///
#[ffi_fallible]
fn evm_try_unsafe_remove_template(template: &mut BlockTemplateWrapper) -> Result<()> {
    // Will be dropped when Box out of scope (end of this fn)
    unsafe {
        let _ = Box::from_raw(template);
    }
    Ok(())
}

/// Add an EVM transaction to a specific block template.
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
/// * `raw_tx` - The raw transaction string.
/// * `hash` - The native transaction hash.
///
/// # Errors
///
/// Returns an Error if:
/// - The `raw_tx` is in invalid format
/// - The block template does not exists.
///
#[ffi_fallible]
fn evm_try_unsafe_push_tx_in_template(
    template: &mut BlockTemplateWrapper,
    raw_tx: &str,
    native_hash: &str,
) -> Result<ffi::ValidateTxCompletion> {
    let native_hash = native_hash.to_string();

    unsafe {
        let signed_tx = SERVICES.evm.core.tx_cache.try_get_or_create(raw_tx)?;

        let tx_hash = signed_tx.hash();
        SERVICES.evm.push_tx_in_block_template(
            template.get_inner_mut()?,
            signed_tx.into(),
            native_hash,
        )?;

        Ok(ffi::ValidateTxCompletion {
            tx_hash: format!("{:?}", tx_hash),
        })
    }
}

/// Creates an EVM block.
///
/// # Arguments
///
/// * `template` - The EVM BlockTemplate.
/// * `difficulty` - The block's difficulty.
/// * `miner_address` - The miner's EVM address as a byte array.
/// * `timestamp` - The block's timestamp.
///
/// # Returns
///
/// Returns a `FinalizeBlockResult` containing the block hash, failed transactions, burnt fees and priority fees (in satoshis) on success.
#[ffi_fallible]
fn evm_try_unsafe_construct_block_in_template(
    template: &mut BlockTemplateWrapper,
    is_miner: bool,
) -> Result<ffi::FinalizeBlockCompletion> {
    unsafe {
        let FinalizedBlockInfo {
            block_hash,
            total_burnt_fees,
            total_priority_fees,
            block_number,
        } = SERVICES
            .evm
            .construct_block_in_template(template.get_inner_mut()?, is_miner)?;
        let total_burnt_fees = u64::try_from(WeiAmount(total_burnt_fees).to_satoshi()?)?;
        let total_priority_fees = u64::try_from(WeiAmount(total_priority_fees).to_satoshi()?)?;

        Ok(ffi::FinalizeBlockCompletion {
            block_hash,
            total_burnt_fees,
            total_priority_fees,
            block_number: u64::try_from(block_number)?,
        })
    }
}

#[ffi_fallible]
fn evm_try_unsafe_commit_block(template: &BlockTemplateWrapper) -> Result<()> {
    unsafe { SERVICES.evm.commit_block(template.get_inner()?) }
}

#[ffi_fallible]
fn evm_try_disconnect_latest_block() -> Result<()> {
    SERVICES.evm.core.clear_account_nonce();
    SERVICES.evm.storage.disconnect_latest_block()
}

#[ffi_fallible]
fn evm_try_unsafe_handle_attribute_apply(
    _template: &mut BlockTemplateWrapper,
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
fn evm_try_get_block_hash_by_number(height: u64) -> Result<XHash> {
    let block = SERVICES
        .evm
        .storage
        .get_block_by_number(&U256::from(height))?
        .ok_or("Invalid block number")?;
    Ok(format!("{:?}", block.header.hash()))
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
fn evm_try_get_block_number_by_hash(hash: &str) -> Result<u64> {
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
fn evm_try_get_block_header_by_hash(hash: &str) -> Result<ffi::EVMBlockHeader> {
    let hash = hash.parse::<H256>().map_err(|_| "Invalid hash")?;

    let block = SERVICES
        .evm
        .storage
        .get_block_by_hash(&hash)?
        .ok_or("Invalid block hash")?;

    let number = u64::try_from(block.header.number)?;
    let gas_limit = u64::try_from(block.header.gas_limit)?;
    let gas_used = u64::try_from(block.header.gas_used)?;
    let base_fee = u64::try_from(WeiAmount(block.header.base_fee).to_satoshi()?)?;

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
fn evm_try_get_tx_by_hash(tx_hash: &str) -> Result<ffi::EVMTransaction> {
    let tx_hash = tx_hash.parse::<H256>().map_err(|_| "Invalid hash")?;

    let tx = SERVICES
        .evm
        .storage
        .get_transaction_by_hash(&tx_hash)?
        .ok_or("Unable to get evm tx from tx hash")?;

    let tx = SERVICES.evm.core.tx_cache.try_get_or_create_from_tx(&tx)?;

    let nonce = u64::try_from(tx.nonce())?;
    let gas_limit = u64::try_from(tx.gas_limit())?;
    let value = u64::try_from(WeiAmount(tx.value()).to_satoshi()?)?;

    let (tx_type, gas_price, max_fee_per_gas, max_priority_fee_per_gas) = match &tx.transaction {
        TransactionV2::Legacy(transaction) => {
            let price = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi()?)?;
            (0u8, price, 0u64, 0u64)
        }
        TransactionV2::EIP2930(transaction) => {
            let price = u64::try_from(WeiAmount(transaction.gas_price).to_satoshi()?)?;
            (1u8, price, 0u64, 0u64)
        }
        TransactionV2::EIP1559(transaction) => {
            let max_fee_per_gas =
                u64::try_from(WeiAmount(transaction.max_fee_per_gas).to_satoshi()?)?;
            let max_priority_fee_per_gas =
                u64::try_from(WeiAmount(transaction.max_priority_fee_per_gas).to_satoshi()?)?;
            (2u8, 0u64, max_fee_per_gas, max_priority_fee_per_gas)
        }
    };

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
fn evm_try_unsafe_create_dst20(
    template: &mut BlockTemplateWrapper,
    native_hash: &str,
    token: ffi::DST20TokenInfo,
) -> Result<()> {
    let native_hash = XHash::from(native_hash);
    let address = ain_contracts::dst20_address_from_token_id(token.id)?;
    debug!("Deploying to address {:#?}", address);

    let system_tx = ExecuteTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        name: token.name,
        symbol: token.symbol,
        address,
        token_id: token.id,
    }));

    unsafe {
        SERVICES
            .evm
            .push_tx_in_block_template(template.get_inner_mut()?, system_tx, native_hash)
    }
}

#[ffi_fallible]
fn evm_try_unsafe_bridge_dst20(
    template: &mut BlockTemplateWrapper,
    raw_tx: &str,
    native_hash: &str,
    token_id: u64,
    out: bool,
) -> Result<()> {
    let native_hash = XHash::from(native_hash);
    let contract_address = ain_contracts::dst20_address_from_token_id(token_id)?;
    let signed_tx = SERVICES.evm.core.tx_cache.try_get_or_create(raw_tx)?;
    let system_tx = ExecuteTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
        signed_tx: Box::new(signed_tx),
        contract_address,
        direction: out.into(),
    }));

    unsafe {
        SERVICES
            .evm
            .push_tx_in_block_template(template.get_inner_mut()?, system_tx, native_hash)
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
fn evm_try_get_tx_hash(raw_tx: &str) -> Result<String> {
    let signed_tx = SERVICES.evm.core.tx_cache.try_get_or_create(raw_tx)?;
    Ok(format!("{:?}", signed_tx.hash()))
}

#[ffi_fallible]
fn evm_try_unsafe_make_signed_tx(raw_tx: &str) -> Result<usize> {
    let ptr = Box::leak(Box::new(SignedTx::try_from(raw_tx)?));
    Ok(ptr as *const SignedTx as usize)
}

#[ffi_fallible]
fn evm_try_unsafe_cache_signed_tx(raw_tx: &str, instance: usize) -> Result<()> {
    let signed_tx = unsafe { Box::from_raw(instance as *mut SignedTx) };
    SERVICES
        .evm
        .core
        .tx_cache
        .pre_populate(raw_tx, *signed_tx)?;
    Ok(())
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
fn evm_try_unsafe_is_smart_contract_in_template(
    address: &str,
    template: &BlockTemplateWrapper,
) -> Result<bool> {
    let address = address.parse::<H160>().map_err(|_| "Invalid address")?;

    unsafe {
        SERVICES
            .evm
            .is_smart_contract_in_block_template(address, template.get_inner()?)
    }
}

#[ffi_fallible]
fn evm_try_get_tx_miner_info_from_raw_tx(raw_tx: &str, mnview_ptr: usize) -> Result<TxMinerInfo> {
    let evm_services = &SERVICES.evm;

    let signed_tx = evm_services.core.tx_cache.try_get_or_create(raw_tx)?;

    let block_service = &evm_services.block;
    let attrs = block_service.get_attribute_vals(Some(mnview_ptr));

    let nonce = u64::try_from(signed_tx.nonce())?;
    let initial_base_fee =
        block_service.calculate_base_fee(H256::zero(), attrs.block_gas_target_factor)?;
    let tip_fee = calculate_max_tip_gas_fee(&signed_tx, initial_base_fee)?;
    let min_rbf_tip_fee =
        calculate_min_rbf_tip_gas_fee(&signed_tx, tip_fee, attrs.rbf_fee_increment)?;

    let tip_fee = u64::try_from(WeiAmount(tip_fee).to_satoshi()?)?;
    let min_rbf_tip_fee = u64::try_from(WeiAmount(min_rbf_tip_fee).to_satoshi()?)?;

    Ok(TxMinerInfo {
        nonce,
        address: format!("{:?}", signed_tx.sender),
        tip_fee,
        min_rbf_tip_fee,
    })
}

#[ffi_fallible]
fn evm_try_dispatch_pending_transactions_event(raw_tx: &str) -> Result<()> {
    let signed_tx = SERVICES.evm.core.tx_cache.try_get_or_create(raw_tx)?;

    debug!(
        "[evm_try_dispatch_pending_transactions_event] {:#?}",
        signed_tx.hash()
    );
    Ok(SERVICES
        .evm
        .channel
        .sender
        .send(Notification::Transaction(signed_tx.hash()))
        .map_err(|e| format_err!(e.to_string()))?)
}
