use anyhow::format_err;
use ethereum_types::U256;

use crate::{transaction::SignedTx, Result};

pub fn calculate_prepay_gas_fee(signed_tx: &SignedTx, base_fee: U256) -> Result<U256> {
    let gas_limit = signed_tx.gas_limit();

    gas_limit
        .checked_mul(signed_tx.effective_gas_price(base_fee))
        .ok_or_else(|| format_err!("calculate prepay gas failed from overflow").into())
}

// Gas prices are denoted in wei
pub fn calculate_gas_fee(signed_tx: &SignedTx, used_gas: U256, base_fee: U256) -> Result<U256> {
    used_gas
        .checked_mul(signed_tx.effective_gas_price(base_fee))
        .ok_or_else(|| format_err!("calculate gas fee failed from overflow").into())
}
