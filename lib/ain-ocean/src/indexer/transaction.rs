use bitcoin::{blockdata::locktime::absolute::LockTime, Txid};
use dftx_rs::Transaction;
use log::debug;

use super::BlockContext;
use crate::{
    indexer::Result,
    model::{
        Transaction as TrasnactionMapper, TransactionVin, TransactionVinScript, TransactionVinVout,
        TransactionVinVoutScript, TransactionVout, TransactionVoutScript,
    },
    repository::RepositoryOps,
    SERVICES,
};

pub fn index_transactions(ctx: &BlockContext, tx: Transaction) -> Result<()> {
    debug!("[CreateTransaction] Indexing...");
    let tx_id = tx.txid();

    let lock_time_as_i32 = match tx.lock_time {
        LockTime::Blocks(value) => value.to_consensus_u32(),
        LockTime::Seconds(value) => value.to_consensus_u32(),
    };
    let total_vout_value: u64 = tx.output.iter().map(|output| output.value.to_sat()).sum();
    let weight = tx.weight();
    let weight_i32 = weight.to_vbytes_ceil() as i32;

    let trx = TrasnactionMapper {
        id: tx_id,
        order: 0,
        block: ctx.clone(),
        txid: tx_id.to_string(),
        hash: ctx.hash.to_string(),
        version: tx.version.0,
        size: tx.total_size() as i32,
        v_size: tx.vsize() as i32,
        weight: weight_i32,
        total_vout_value: total_vout_value.to_string(),
        lock_time: lock_time_as_i32 as i32,
        vin_count: tx.input.len() as i32,
        vout_count: tx.output.len() as i32,
    };
    // Index transaction
    SERVICES.transaction.by_id.put(&tx_id, &trx)?;
    // Index transaction vin
    for (vin_idx, vin) in tx.input.iter().enumerate() {
        let trx_vin = TransactionVin {
            id: format!("{}-{}", tx_id, vin_idx),
            txid: tx_id.to_string(),
            coinbase: vin.script_sig.to_string(),
            vout: TransactionVinVout {
                id: format!("{}-{}-vout", tx_id, vin_idx),
                txid: tx_id.to_string(),
                n: vin.previous_output.vout as i32,
                value: "0".to_string(),
                token_id: 0,
                script: TransactionVinVoutScript {
                    hex: vin.script_sig.to_string(),
                },
            },
            script: TransactionVinScript {
                hex: vin.script_sig.to_string(),
            },
            tx_in_witness: vec![],
            sequence: vin.sequence.to_string(),
        };

        SERVICES
            .transaction
            .vin_by_id
            .put(&tx_id.to_string(), &trx_vin)?;
    }
    // Index transaction vout
    for (vout_idx, vout) in tx.output.iter().enumerate() {
        let trx_vout = TransactionVout {
            id: format!("{}-{}", tx_id, vout_idx),
            txid: tx_id.to_string(),
            n: vout_idx as i32,
            value: vout.value.to_string(),
            token_id: 0,
            script: TransactionVoutScript {
                hex: vout.script_pubkey.to_string(),
                r#type: "pubkey".to_string(),
            },
        };
        SERVICES
            .transaction
            .vout_by_id
            .put(&tx_id.to_string(), &trx_vout)?;
        // .put(&format!("{}-{}", tx_id, vout_idx), &trx_vout)?; //need
    }

    Ok(())
}

pub fn invalidate_transaction(ctx: &BlockContext, tx: Txid, idx: usize) -> Result<()> {
    debug!("[CreateMasternode] Invalidating...");
    SERVICES.transaction.by_id.delete(&tx)?;
    Ok(())
}
