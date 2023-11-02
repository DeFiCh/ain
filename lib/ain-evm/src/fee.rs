use anyhow::format_err;
use ethereum::TransactionV2;
use ethereum_types::U256;

use crate::{transaction::SignedTx, Result};

const MEMPOOL_MINIMUM_RBF_FEE_PER_GAS: U256 = U256([1_000_000_000, 0, 0, 0]);

pub fn calculate_max_tip_gas_fee(signed_tx: &SignedTx, base_fee: U256) -> Result<U256> {
    match &signed_tx.transaction {
        TransactionV2::Legacy(tx) => {
            let tip_fee_per_gas = tx.gas_price.saturating_sub(base_fee);
            tx.gas_limit.checked_mul(tip_fee_per_gas)
        }
        TransactionV2::EIP2930(tx) => {
            let tip_fee_per_gas = tx.gas_price.saturating_sub(base_fee);
            tx.gas_limit.checked_mul(tip_fee_per_gas)
        }
        TransactionV2::EIP1559(tx) => tx.gas_limit.checked_mul(tx.max_priority_fee_per_gas),
    }
    .ok_or_else(|| format_err!("calculate max tip fee failed from overflow").into())
}

pub fn calculate_min_rbf_tip_gas_fee(
    signed_tx: &SignedTx,
    tip_fee: U256,
    rbf_fee_increment: u64,
) -> Result<U256> {
    let mempool_increment_percentage = U256::from(rbf_fee_increment / 1_000_000);

    let incremental_fee = tip_fee
        .checked_mul(mempool_increment_percentage)
        .and_then(|f| f.checked_div(U256::from(100)))
        .ok_or_else(|| format_err!("calculate incremental fees failed from overflow"))?;
    let min_incremental_fee = signed_tx
        .gas_limit()
        .checked_mul(MEMPOOL_MINIMUM_RBF_FEE_PER_GAS)
        .ok_or_else(|| format_err!("calculate incremental fees failed from overflow"))?;
    let additional_fee = if incremental_fee < min_incremental_fee {
        min_incremental_fee
    } else {
        incremental_fee
    };

    tip_fee
        .checked_add(additional_fee)
        .ok_or_else(|| format_err!("calculate incremental fees failed from overflow").into())
}

pub fn calculate_current_prepay_gas_fee(signed_tx: &SignedTx, base_fee: U256) -> Result<U256> {
    let gas_limit = signed_tx.gas_limit();

    gas_limit
        .checked_mul(signed_tx.effective_gas_price(base_fee)?)
        .ok_or_else(|| format_err!("calculate current prepay fee failed from overflow").into())
}

pub fn calculate_max_prepay_gas_fee(signed_tx: &SignedTx) -> Result<U256> {
    match &signed_tx.transaction {
        TransactionV2::Legacy(tx) => tx.gas_limit.checked_mul(tx.gas_price),
        TransactionV2::EIP2930(tx) => tx.gas_limit.checked_mul(tx.gas_price),
        TransactionV2::EIP1559(tx) => tx.gas_limit.checked_mul(tx.max_fee_per_gas),
    }
    .ok_or_else(|| format_err!("calculate max prepay fee failed from overflow").into())
}

// Gas prices are denoted in wei
pub fn calculate_gas_fee(signed_tx: &SignedTx, used_gas: U256, base_fee: U256) -> Result<U256> {
    used_gas
        .checked_mul(signed_tx.effective_gas_price(base_fee)?)
        .ok_or_else(|| format_err!("calculate gas fee failed from overflow").into())
}
