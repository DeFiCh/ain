use anyhow::format_err;
use ethereum::TransactionV2;
use ethereum_types::U256;

use crate::{transaction::SignedTx, Result};

pub fn calculate_prepay_gas_fee(signed_tx: &SignedTx, base_fee: U256) -> Result<U256> {
    let effective_gas_price = signed_tx.effective_gas_price(base_fee);
    match &signed_tx.transaction {
        TransactionV2::Legacy(tx) => tx.gas_limit.checked_mul(effective_gas_price),
        TransactionV2::EIP2930(tx) => tx.gas_limit.checked_mul(effective_gas_price),
        TransactionV2::EIP1559(tx) => tx.gas_limit.checked_mul(effective_gas_price),
    }
    .ok_or_else(|| format_err!("calculate prepay gas failed from overflow").into())
}

// Gas prices are denoted in wei
pub fn calculate_gas_fee(signed_tx: &SignedTx, used_gas: U256, base_fee: U256) -> Result<U256> {
    used_gas
        .checked_mul(signed_tx.effective_gas_price(base_fee))
        .ok_or_else(|| format_err!("calculate gas fee failed from overflow").into())
}
