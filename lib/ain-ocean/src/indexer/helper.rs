use bitcoin::{hashes::Hash, Txid};
use defichain_rpc::json::blockchain::{Transaction, Vin};

pub fn check_if_evm_tx(txn: &Transaction) -> bool {
    txn.vin.len() == 2
        && txn.vin.iter().all(|vin| match vin {
            Vin::Coinbase(_) => true,
            Vin::Standard(tx) => tx.txid == Txid::all_zeros(),
        })
        && txn.vout.len() == 1
        && txn.vout[0]
            .script_pub_key
            .asm
            .starts_with("OP_RETURN 4466547839")
        && txn.vout[0].value == 0f64
}
