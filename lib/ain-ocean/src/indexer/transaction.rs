use std::sync::Arc;

use bitcoin::{hashes::Hash, Txid};
use defichain_rpc::json::blockchain::{Transaction, Vin};
use log::debug;

use super::Context;
use crate::{
    indexer::Result,
    model::{
        Transaction as TransactionMapper, TransactionVin, TransactionVout, TransactionVoutScript,
    },
    repository::RepositoryOps,
    Services,
};

pub fn index_transaction(services: &Arc<Services>, ctx: Context) -> Result<()> {
    debug!("[index_transaction] Indexing...");
    let idx = ctx.tx_idx;
    let is_evm = check_if_evm_tx(&ctx.tx);

    let txid = ctx.tx.txid;
    let vin_count = ctx.tx.vin.len();
    let vout_count = ctx.tx.vout.len();

    let mut total_vout_value = 0f64;
    let mut vouts = Vec::with_capacity(vout_count);
    // Index transaction vout
    for (vout_idx, vout) in ctx.tx.vout.into_iter().enumerate() {
        let tx_vout = TransactionVout {
            txid: txid,
            n: vout_idx,
            value: vout.value,
            token_id: 0,
            script: TransactionVoutScript {
                hex: vout.script_pub_key.hex,
                r#type: vout.script_pub_key.r#type,
            },
        };
        services
            .transaction
            .vout_by_id
            .put(&(txid, vout_idx), &tx_vout)?;

        total_vout_value += vout.value;
        vouts.push(tx_vout);
    }

    // Indexing transaction vin
    for vin in ctx.tx.vin.into_iter() {
        if is_evm {
            continue;
        }

        let vin = TransactionVin::from_vin_and_txid(vin, txid, &vouts);
        services.transaction.vin_by_id.put(&vin.id, &vin)?;
    }

    let tx = TransactionMapper {
        id: txid,
        order: idx,
        hash: ctx.tx.hash.clone(),
        block: ctx.block.clone(),
        version: ctx.tx.version,
        size: ctx.tx.size,
        v_size: ctx.tx.vsize,
        weight: ctx.tx.weight,
        total_vout_value,
        lock_time: ctx.tx.locktime,
        vin_count,
        vout_count,
    };
    // Index transaction
    services.transaction.by_id.put(&txid, &tx)?;

    Ok(())
}

fn check_if_evm_tx(txn: &Transaction) -> bool {
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
