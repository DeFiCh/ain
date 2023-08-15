use std::cmp;

use anyhow::format_err;
use ethereum::TransactionV2;
use ethereum_types::U256;

use crate::transaction::SignedTx;
use crate::Result;

pub fn calculate_prepay_gas_fee(signed_tx: &SignedTx) -> Result<U256> {
    match &signed_tx.transaction {
        TransactionV2::Legacy(tx) => tx.gas_limit.checked_mul(tx.gas_price),
        TransactionV2::EIP2930(tx) => tx.gas_limit.checked_mul(tx.gas_price),
        TransactionV2::EIP1559(tx) => tx.gas_limit.checked_mul(tx.max_fee_per_gas),
    }
    .ok_or_else(|| format_err!("calculate prepay gas failed from overflow").into())
}

// Gas prices are denoted in wei
pub fn calculate_gas_fee(signed_tx: &SignedTx, used_gas: U256, base_fee: U256) -> Result<U256> {
    match &signed_tx.transaction {
        TransactionV2::Legacy(tx) => used_gas.checked_mul(tx.gas_price),
        TransactionV2::EIP2930(tx) => used_gas.checked_mul(tx.gas_price),
        TransactionV2::EIP1559(tx) => {
            let gas_fee = cmp::min(tx.max_fee_per_gas, tx.max_priority_fee_per_gas + base_fee);
            used_gas.checked_mul(gas_fee)
        }
    }
    .ok_or_else(|| format_err!("calculate gas fee failed from overflow").into())
}
