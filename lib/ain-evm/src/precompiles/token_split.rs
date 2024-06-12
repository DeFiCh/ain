use std::collections::BTreeMap;

use ain_contracts::{dst20_address_from_token_id, validate_split_tokens_input, TokenSplitParams};
use ain_cpp_imports::{split_tokens_from_evm, TokenAmount};
use anyhow::format_err;
use ethereum_types::{H160, H256, U256};
use evm::{
    backend::Apply,
    executor::stack::{PrecompileFailure, PrecompileHandle, PrecompileOutput},
    ExitError, ExitSucceed,
};
use log::trace;

use super::DVMStatePrecompile;
use crate::{
    contract::{get_address_storage_index, u256_to_h256},
    precompiles::PrecompileResult,
    weiamount::{try_from_satoshi, WeiAmount},
    Result,
};

pub struct TokenSplit;

impl TokenSplit {
    const GAS_COST: u64 = 1000;
}

impl DVMStatePrecompile for TokenSplit {
    fn execute(handle: &mut impl PrecompileHandle, mnview_ptr: usize) -> PrecompileResult {
        handle.record_cost(TokenSplit::GAS_COST)?;

        let input = handle.input();

        let TokenSplitParams {
            sender,
            token_contract: original_contract,
            amount: input_amount,
            ..
        } = validate_split_tokens_input(input).map_err(|e| PrecompileFailure::Error {
            exit_status: ExitError::Other(e.to_string().into()),
        })?;

        trace!("[TokenSplit] sender {sender:x}, original_contract {original_contract:x}, input_amount : {input_amount:x}");

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

        let old_token_id = (contract_value - contract_base).low_u64() as u32;

        let old_amount = TokenAmount {
            id: old_token_id,
            amount: amount.low_u64(),
        };

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

        let output = {
            let mut bytes = [0u8; 64];
            bytes[12..32].copy_from_slice(new_contract.as_bytes());
            converted_amount.0.to_big_endian(&mut bytes[32..]);
            bytes.to_vec()
        };

        // No split took place
        if new_amount.id == old_token_id {
            return Ok(PrecompileOutput {
                exit_status: ExitSucceed::Returned,
                state_changes: None,
                output,
            });
        }

        let Ok(storage) = get_new_contract_storage_update(
            handle,
            original_contract,
            new_contract,
            converted_amount.0,
        ) else {
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

        Ok(PrecompileOutput {
            exit_status: ExitSucceed::Returned,
            state_changes: Some(vec![new_contract_state_changes]),
            output,
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
