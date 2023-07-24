use crate::transaction::SignedTx;
use ethereum::TransactionAction;
use evm::Config;
use evm_gasometer::{call_transaction_cost, create_transaction_cost, Gasometer, TransactionCost};

use anyhow::anyhow;
use log::debug;
use std::error::Error;

// Changi intermediate constant 
pub const MIN_GAS_PER_TX: u64 = 21_000;

fn get_tx_cost(signed_tx: &SignedTx) -> TransactionCost {
    let access_list = signed_tx
        .access_list()
        .into_iter()
        .map(|x| (x.address, x.storage_keys))
        .collect::<Vec<_>>();

    match signed_tx.action() {
        TransactionAction::Call(_) => call_transaction_cost(signed_tx.data(), &access_list),
        TransactionAction::Create => create_transaction_cost(signed_tx.data(), &access_list),
    }
}

pub fn check_tx_intrinsic_gas(signed_tx: &SignedTx) -> Result<(), Box<dyn Error>> {
    const CONFIG: Config = Config::shanghai();
    let mut gasometer = Gasometer::new(signed_tx.gas_limit().as_u64(), &CONFIG);

    let tx_cost = get_tx_cost(signed_tx);
    match gasometer.record_transaction(tx_cost) {
        Err(_) => {
            debug!("[check_tx_intrinsic_gas] gas limit is below the minimum gas per tx");
            return Err(anyhow!("gas limit is below the minimum gas per tx").into())
        }
        _ => Ok(()),
    }
}
