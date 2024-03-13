use std::collections::BTreeMap;

use ain_contracts::{dst20_address_from_token_id, validate_split_tokens_input, TokenSplitParams};
use ain_cpp_imports::{bridge::ffi::TokenAmount, split_tokens_from_evm};
use ethereum_types::{H160, H256, U256};
use evm::{
    backend::Apply,
    executor::stack::{PrecompileFailure, PrecompileHandle, PrecompileOutput},
    ExitError, ExitSucceed,
};

use crate::{
    contract::{get_address_storage_index, u256_to_h256},
    precompiles::PrecompileResult,
    weiamount::{try_from_satoshi, WeiAmount},
    Result,
};

use anyhow::format_err;
use log::debug;

use super::DVMStatePrecompile;

pub struct TokenSplit;

impl TokenSplit {
    const GAS_COST: u64 = 1000;
}

impl DVMStatePrecompile for TokenSplit {
    fn execute(handle: &mut impl PrecompileHandle, mnview_ptr: usize) -> PrecompileResult {
        debug!("[TokenSplit]");
        handle.record_cost(TokenSplit::GAS_COST)?;

        let input = handle.input();

        let TokenSplitParams {
            sender,
            token_contract: original_contract,
            amount: input_amount,
        } = validate_split_tokens_input(input).map_err(|e| PrecompileFailure::Error {
            exit_status: ExitError::Other(e.to_string().into()),
        })?;

        debug!("[TokenSplit] sender {sender:x}, original_contract {original_contract:x}, input_amount : {input_amount:x}");

        let Ok(amount) = WeiAmount(input_amount).to_satoshi() else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Failed to convert amount into Sats".into()),
            });
        };

        let contract_value = U256::from_big_endian(original_contract.as_bytes());
        let Ok(contract_base) =
            U256::from_str_radix("df00000000000000000000000000000000000000", 16)
        else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Failed to convert base amount into U256".into()),
            });
        };

        let dvm_id = (contract_value - contract_base).low_u64() as u32;

        let old_amount = TokenAmount {
            id: dvm_id,
            amount: amount.low_u64(),
        };
        debug!("[TokenSplit] old_amount : {:?}", old_amount);

        let mut new_amount = TokenAmount { id: 0, amount: 0 };

        let res = split_tokens_from_evm(mnview_ptr, old_amount, &mut new_amount);

        if !res {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Failed to split tokens".into()),
            });
        }

        let Ok(converted_amount) = try_from_satoshi(U256::from(new_amount.amount)) else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Failed to convert new Sats amount into Wei".into()),
            });
        };

        let Ok(new_contract) = dst20_address_from_token_id(new_amount.id.into()) else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Error getting DST20 from new_amount.id".into()),
            });
        };

        let Ok(storage) =
            get_new_contract_storage_update(handle, sender, new_contract, converted_amount.0)
        else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Error getting storage update".into()),
            });
        };

        let new_contract_state_changes = Apply::Modify {
            address: new_contract,
            basic: handle.basic(new_contract),
            code: None,
            storage,
            reset_storage: false,
        };

        let Ok(storage) =
            get_original_contract_storage_update(handle, sender, original_contract, input_amount)
        else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Error getting storage update".into()),
            });
        };

        let original_contract_state_changes = Apply::Modify {
            address: original_contract,
            basic: handle.basic(original_contract),
            code: None,
            storage,
            reset_storage: false,
        };

        Ok(PrecompileOutput {
            exit_status: ExitSucceed::Returned,
            state_changes: Some(vec![
                original_contract_state_changes,
                new_contract_state_changes,
            ]),
            output: Vec::new(),
        })
    }
}

fn get_new_contract_storage_update(
    handle: &mut impl PrecompileHandle,
    sender: H160,
    contract: H160,
    amount: U256,
) -> Result<BTreeMap<H256, H256>> {
    let contract_balance_storage_index = get_address_storage_index(H256::zero(), sender);
    let sender_balance = U256::from(
        handle
            .storage(contract, contract_balance_storage_index)
            .as_bytes(),
    );

    let new_sender_balance = sender_balance
        .checked_add(amount)
        .ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    let contract_allowance_storage_index =
        get_address_storage_index(H256::from_low_u64_be(1), sender);
    let sender_allowance = U256::from(
        handle
            .storage(contract, contract_allowance_storage_index)
            .as_bytes(),
    );

    let new_sender_allowance = sender_allowance
        .checked_add(amount)
        .ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    let total_supply_index = H256::from_low_u64_be(2);
    let total_supply = U256::from(handle.storage(contract, total_supply_index).as_bytes());

    let new_total_supply = total_supply
        .checked_add(amount)
        .ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    Ok(BTreeMap::from([
        (
            contract_balance_storage_index,
            u256_to_h256(new_sender_balance),
        ),
        (
            contract_allowance_storage_index,
            u256_to_h256(new_sender_allowance),
        ),
        (total_supply_index, u256_to_h256(new_total_supply)),
    ]))
}

fn get_original_contract_storage_update(
    handle: &mut impl PrecompileHandle,
    sender: H160,
    contract: H160,
    amount: U256,
) -> Result<BTreeMap<H256, H256>> {
    let contract_balance_storage_index = get_address_storage_index(H256::zero(), sender);
    let sender_balance = U256::from(
        handle
            .storage(contract, contract_balance_storage_index)
            .as_bytes(),
    );

    debug!("sender_balance : {}", sender_balance);

    let new_sender_balance = sender_balance
        .checked_sub(amount)
        .ok_or_else(|| format_err!("Total supply overflow/underflow"))?;
    debug!("new_sender_balance : {:x}", new_sender_balance);

    let total_supply_index = H256::from_low_u64_be(2);
    let total_supply = U256::from(handle.storage(contract, total_supply_index).as_bytes());

    let new_total_supply = total_supply
        .checked_sub(amount)
        .ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    debug!("new_total_supply : {:x}", new_total_supply);

    Ok(BTreeMap::from([
        (
            contract_balance_storage_index,
            u256_to_h256(new_sender_balance),
        ),
        (total_supply_index, u256_to_h256(new_total_supply)),
    ]))
}
