use std::cmp;
use std::error::Error;

use anyhow::format_err;
use ethereum_types::U256;

use crate::transaction::SignedTx;

pub fn calculate_prepay_gas_fee(signed_tx: &SignedTx) -> Result<U256, Box<dyn Error>> {
    let prepay_gas = match &signed_tx.transaction {
        ethereum::TransactionV2::Legacy(tx) => tx.gas_limit.checked_mul(tx.gas_price),
        ethereum::TransactionV2::EIP2930(tx) => tx.gas_limit.checked_mul(tx.gas_price),
        ethereum::TransactionV2::EIP1559(tx) => tx.gas_limit.checked_mul(tx.max_fee_per_gas),
    };

    match prepay_gas {
        Some(gas) => Ok(gas),
        None => Err(format_err!("calculate prepay gas failed from overflow").into()),
    }
}

// Gas prices are denoted in wei
pub fn calculate_gas_fee(
    signed_tx: &SignedTx,
    used_gas: U256,
    base_fee: U256,
) -> Result<U256, Box<dyn Error>> {
    let gas_fee = match &signed_tx.transaction {
        ethereum::TransactionV2::Legacy(tx) => used_gas.checked_mul(tx.gas_price),
        ethereum::TransactionV2::EIP2930(tx) => used_gas.checked_mul(tx.gas_price),
        ethereum::TransactionV2::EIP1559(tx) => {
            let gas_fee = cmp::min(tx.max_fee_per_gas, tx.max_priority_fee_per_gas + base_fee);
            used_gas.checked_mul(gas_fee)
        }
    };

    match gas_fee {
        Some(fee) => Ok(fee),
        None => Err(format_err!("calculate gas fee failed from overflow").into()),
    }
}

// Gas prices are denoted in wei
pub fn get_tx_max_gas_price(signed_tx: &SignedTx) -> U256 {
    match &signed_tx.transaction {
        ethereum::TransactionV2::Legacy(tx) => tx.gas_price,
        ethereum::TransactionV2::EIP2930(tx) => tx.gas_price,
        ethereum::TransactionV2::EIP1559(tx) => tx.max_fee_per_gas,
    }
}
