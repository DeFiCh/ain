use ain_cpp_imports::{bridge::ffi::TokenAmount, split_tokens_from_evm};
use ethereum_types::U256;
use evm::{
    executor::stack::{PrecompileFailure, PrecompileHandle, PrecompileOutput},
    ExitError, ExitSucceed,
};

use crate::{
    precompiles::{Precompile, PrecompileResult},
    weiamount::{WeiAmount, try_from_satoshi},
};

use log::debug;

pub struct TokenSplit;

impl TokenSplit {
    const GAS_COST: u64 = 1000;
}

impl Precompile for TokenSplit {
    fn execute(handle: &mut impl PrecompileHandle) -> PrecompileResult {
        handle.record_cost(TokenSplit::GAS_COST)?;

        let input = handle.input();

        let _sender_address_bytes: [u8; 20] = match input[12..32].try_into() {
            Ok(v) => v,
            Err(e) => {
                return Err(PrecompileFailure::Error {
                    exit_status: ExitError::Other(e.to_string().into()),
                })
            }
        };

        let Ok(amount) = WeiAmount(U256::from_big_endian(&input[64..])).to_satoshi() else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Failed to convert amount into Sats".into()),
            });
        };

        let contract_value = U256::from_big_endian(&input[32..64]);
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
        let mut new_amount = TokenAmount { id: 0, amount: 0 };

        let res = split_tokens_from_evm(old_amount, &mut new_amount);

        let Ok(converted_amount) = try_from_satoshi(U256::from(new_amount.amount)) else {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Failed to convert new Sats amount into Wei".into()),
            });
        
        };

        debug!(
            "XXX new token amount {} id {} converted {}",
            new_amount.amount, new_amount.id, converted_amount.0
        );

        if !res {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::Other("Failed to split tokens".into()),
            });
        }

        Ok(PrecompileOutput {
            exit_status: ExitSucceed::Returned,
            output: Vec::new(),
        })
    }
}
