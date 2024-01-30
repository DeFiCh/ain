use bitcoin::{blockdata::locktime::absolute::LockTime, hashes::Hash, Amount, Txid};
use dftx_rs::Transaction;
use log::debug;

use super::BlockContext;
use crate::{
    indexer::Result,
    model::{
        Transaction as TransactionMapper, TransactionVin, TransactionVinScript, TransactionVinVout,
        TransactionVinVoutScript, TransactionVout, TransactionVoutScript,
    },
    repository::RepositoryOps,
    SERVICES,
};

pub fn index_transaction(ctx: &BlockContext, tx: &Transaction, idx: usize) -> Result<()> {
    debug!("[index_transaction] Indexing...");
    let tx_id = tx.txid();
    let is_evm = check_if_evm_tx(&tx);
    let lock_time = match tx.lock_time {
        LockTime::Blocks(value) => value.to_consensus_u32(),
        LockTime::Seconds(value) => value.to_consensus_u32(),
    };

    // Indexing transaction vin
    for (vin_idx, vin) in tx.input.iter().enumerate() {
        if is_evm {
            continue;
        }
        let vout_bytes = vin.previous_output.vout.to_be_bytes();
        let trx_vin = TransactionVin {
            id: (tx_id, vin.previous_output.txid, vout_bytes),
            txid: tx_id,
            coinbase: vin.previous_output.to_string(),
            vout: TransactionVinVout {
                id: (tx_id, vin_idx),
                txid: tx_id,
                n: vin.sequence.0 as i32,
                value: vin.previous_output.vout,
                token_id: 0,
                script: TransactionVinVoutScript {
                    hex: vin.script_sig.clone(),
                },
            },
            script: TransactionVinScript {
                hex: vin.script_sig.clone(),
            },
            tx_in_witness: vec![],
            sequence: vin.sequence,
        };

        SERVICES.transaction.vin_by_id.put(&tx_id, &trx_vin)?;
    }

    let mut total_vout_value = 0;
    // Index transaction vout
    for (vout_idx, vout) in tx.output.iter().enumerate() {
        let trx_vout = TransactionVout {
            txid: tx_id,
            n: vout_idx,
            value: vout.value,
            token_id: 0,
            script: TransactionVoutScript {
                hex: vout.script_pubkey.clone(),
                r#type: vout.script_pubkey.to_hex_string(),
            },
        };
        SERVICES
            .transaction
            .vout_by_id
            .put(&(tx_id, vout_idx), &trx_vout)?;

        total_vout_value += vout.value.to_sat();
    }

    let trx = TransactionMapper {
        id: tx_id,
        order: idx,
        block: ctx.clone(),
        hash: ctx.hash,
        version: tx.version.0,
        size: tx.total_size(),
        v_size: tx.vsize(),
        weight: tx.weight().to_wu(),
        total_vout_value,
        lock_time: lock_time,
        vin_count: tx.input.len(),
        vout_count: tx.output.len(),
    };
    // Index transaction
    SERVICES.transaction.by_id.put(&tx_id, &trx)?;

    Ok(())
}

fn check_if_evm_tx(txn: &Transaction) -> bool {
    txn.input.len() == 2
        && txn
            .input
            .iter()
            .all(|vin| vin.previous_output.txid == Txid::all_zeros())
        && txn.output.len() == 1
        && txn.output[0]
            .script_pubkey
            .to_asm_string()
            .starts_with("OP_RETURN 4466547839")
        && txn.output[0].value == Amount::from_sat(0)
}
