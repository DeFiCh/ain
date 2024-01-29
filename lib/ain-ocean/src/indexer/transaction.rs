use bitcoin::{blockdata::locktime::absolute::LockTime, Txid};
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

pub fn index_transaction(ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
    debug!("[index_transaction] Indexing...");
    let tx_id = tx.txid();

    let lock_time = match tx.lock_time {
        LockTime::Blocks(value) => value.to_consensus_u32(),
        LockTime::Seconds(value) => value.to_consensus_u32(),
    };
    let total_vout_value = tx.output.iter().map(|output| output.value.to_sat()).sum();

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
    // Indexing transaction vin
    for (vin_idx, vin) in tx.input.iter().enumerate() {
        let vout_bytes = vin.previous_output.vout.to_be_bytes();
        let trx_vin = TransactionVin {
            id: format!(
                "{}-{}-{}",
                tx_id,
                vin.previous_output.txid,
                hex::encode(vout_bytes)
            ),
            txid: tx_id,
            coinbase: vin.previous_output.to_string(),
            vout: TransactionVinVout {
                id: format!("{}-{}-vout", tx_id, vin_idx),
                txid: tx_id,
                n: vin.previous_output.vout as i32,
                value: "0".to_string(),
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
    // Index transaction vout
    for (vout_idx, vout) in tx.output.iter().enumerate() {
        let vout_index = vout_idx.to_be_bytes();
        let trx_vout = TransactionVout {
            id: format!("{}-{}", tx_id, vout_idx),
            txid: tx_id,
            n: vout_idx as i32,
            value: vout.value.to_string(),
            token_id: 0,
            script: TransactionVoutScript {
                hex: vout.script_pubkey.clone(),
                r#type: "pubkey".to_string(),
            },
        };
        SERVICES
            .transaction
            .vout_by_id
            .put(&format!("{}-{}", tx_id, hex::encode(vout_index)), &trx_vout)?;
    }

    Ok(())
}

pub fn invalidate_transaction(tx_id: Txid) -> Result<()> {
    debug!("[invalidate_transaction] Invalidating...");
    SERVICES.transaction.by_id.delete(&tx_id)?;
    Ok(())
}

//key: txid + vout.txid + (vin.previous_output.vout 4 bytes encoded hex)
pub fn invalidate_transaction_vin(tx_id: String) -> Result<()> {
    debug!("[invalidate_transaction] Invalidating...");
    SERVICES.transaction.vout_by_id.delete(&tx_id)?;
    Ok(())
}

//key: which is string type (txid + encoded (vout_idx)
pub fn invalidate_transaction_vout(tx_id: String) -> Result<()> {
    debug!("[invalidate_transaction] Invalidating...");
    SERVICES.transaction.vout_by_id.delete(&tx_id)?;
    Ok(())
}
