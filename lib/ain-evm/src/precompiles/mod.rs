mod blake2;
mod bn128;
mod modexp;
mod simple;

#[cfg(test)]
mod test_vector_support;

use blake2::Blake2F;
use bn128::{Bn128Add, Bn128Mul, Bn128Pairing};
use ethereum_types::H160;
use evm::executor::stack::IsPrecompileResult;
pub use evm::{
    executor::stack::{PrecompileFailure, PrecompileHandle, PrecompileOutput, PrecompileSet},
    Context, ExitError, ExitRevert, ExitSucceed, Transfer,
};
use modexp::Modexp;
use simple::{ECRecover, Identity, Ripemd160, Sha256};

type PrecompileResult = Result<PrecompileOutput, PrecompileFailure>;

/// One single precompile used by EVM engine.
pub trait Precompile {
    /// Try to execute the precompile with given `handle` which provides all call data
    /// and allow to register costs and logs.
    fn execute(handle: &mut impl PrecompileHandle) -> PrecompileResult;
}

pub trait LinearCostPrecompile {
    const BASE: u64;
    const WORD: u64;

    fn execute(
        input: &[u8],
        cost: u64,
    ) -> core::result::Result<(ExitSucceed, Vec<u8>), PrecompileFailure>;
}

impl<T: LinearCostPrecompile> Precompile for T {
    fn execute(handle: &mut impl PrecompileHandle) -> PrecompileResult {
        let target_gas = handle.gas_limit();
        let cost = ensure_linear_cost(target_gas, handle.input().len() as u64, T::BASE, T::WORD)?;

        handle.record_cost(cost)?;
        let (exit_status, output) = T::execute(handle.input(), cost)?;
        Ok(PrecompileOutput {
            exit_status,
            output,
        })
    }
}

/// Linear gas cost
fn ensure_linear_cost(
    target_gas: Option<u64>,
    len: u64,
    base: u64,
    word: u64,
) -> Result<u64, PrecompileFailure> {
    let cost = base
        .checked_add(word.checked_mul(len.saturating_add(31) / 32).ok_or(
            PrecompileFailure::Error {
                exit_status: ExitError::OutOfGas,
            },
        )?)
        .ok_or(PrecompileFailure::Error {
            exit_status: ExitError::OutOfGas,
        })?;

    if let Some(target_gas) = target_gas {
        if cost > target_gas {
            return Err(PrecompileFailure::Error {
                exit_status: ExitError::OutOfGas,
            });
        }
    }

    Ok(cost)
}

#[derive(Default)]
pub struct MetachainPrecompiles;

// Ethereum precompiles available as of shangai fork :
// Ref: Ethereum Yellow Paper (https://ethereum.github.io/yellowpaper/paper.pdf) Page 12
impl MetachainPrecompiles {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn used_addresses() -> [H160; 9] {
        [
            hash(1),
            hash(2),
            hash(3),
            hash(4),
            hash(5),
            hash(6),
            hash(7),
            hash(8),
            hash(9),
        ]
    }
}

impl PrecompileSet for MetachainPrecompiles {
    fn execute(&self, handle: &mut impl PrecompileHandle) -> Option<PrecompileResult> {
        match handle.code_address() {
            a if a == hash(1) => Some(<ECRecover as Precompile>::execute(handle)),
            a if a == hash(2) => Some(<Sha256 as Precompile>::execute(handle)),
            a if a == hash(3) => Some(<Ripemd160 as Precompile>::execute(handle)),
            a if a == hash(4) => Some(<Identity as Precompile>::execute(handle)),
            a if a == hash(5) => Some(Modexp::execute(handle)),
            a if a == hash(6) => Some(Bn128Add::execute(handle)),
            a if a == hash(7) => Some(Bn128Mul::execute(handle)),
            a if a == hash(8) => Some(Bn128Pairing::execute(handle)),
            a if a == hash(9) => Some(Blake2F::execute(handle)),
            _ => None,
        }
    }

    fn is_precompile(&self, address: H160, _gas: u64) -> IsPrecompileResult {
        IsPrecompileResult::Answer {
            is_precompile: Self::used_addresses().contains(&address),
            extra_cost: 0,
        }
    }
}

fn hash(a: u64) -> H160 {
    H160::from_low_u64_be(a)
}
