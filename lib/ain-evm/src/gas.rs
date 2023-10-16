use anyhow::format_err;
use ethereum::TransactionAction;
use evm::{
    gasometer::{call_transaction_cost, create_transaction_cost, Gasometer, TransactionCost},
    Config,
};
use log::debug;

use crate::{transaction::SignedTx, Result};

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

pub fn check_tx_intrinsic_gas(signed_tx: &SignedTx) -> Result<()> {
    const CONFIG: Config = Config::shanghai();
    let mut gasometer = Gasometer::new(u64::try_from(signed_tx.gas_limit())?, &CONFIG);

    let tx_cost = get_tx_cost(signed_tx);
    match gasometer.record_transaction(tx_cost) {
        Err(_) => {
            debug!("[check_tx_intrinsic_gas] gas limit is below the minimum gas per tx");
            Err(format_err!("gas limit is below the minimum gas per tx").into())
        }
        _ => Ok(()),
    }
}
