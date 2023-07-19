use crate::transaction::SignedTx;
use ethereum_types::U256;
use std::cmp;

// Gas prices are denoted in wei
pub fn calculate_prepay_gas(signed_tx: &SignedTx) -> U256 {
    match &signed_tx.transaction {
        ethereum::TransactionV2::Legacy(tx) => tx.gas_limit.saturating_mul(tx.gas_price),
        ethereum::TransactionV2::EIP2930(tx) => tx.gas_limit.saturating_mul(tx.gas_price),
        ethereum::TransactionV2::EIP1559(tx) => tx.gas_limit.saturating_mul(tx.max_fee_per_gas),
    }
}

// Gas prices are denoted in wei
pub fn calculate_gas_fee(signed_tx: &SignedTx, used_gas: U256, base_fee: U256) -> U256 {
    match &signed_tx.transaction {
        ethereum::TransactionV2::Legacy(tx) => used_gas.saturating_mul(tx.gas_price),
        ethereum::TransactionV2::EIP2930(tx) => used_gas.saturating_mul(tx.gas_price),
        ethereum::TransactionV2::EIP1559(tx) => {
            let gas_fee = cmp::min(tx.max_fee_per_gas, tx.max_priority_fee_per_gas + base_fee);
            used_gas.saturating_mul(gas_fee)
        }
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
