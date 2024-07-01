use std::sync::Arc;

use log::debug;
use rust_decimal::{
    prelude::{FromPrimitive, Zero},
    Decimal,
};

use super::{helper::check_if_evm_tx, Context};
use crate::{
    error::Error,
    indexer::Result,
    model::{
        Transaction as TransactionMapper, TransactionVin, TransactionVout, TransactionVoutScript,
    },
    repository::RepositoryOps,
    Services,
};

pub fn index_transaction(services: &Arc<Services>, ctx: &Context) -> Result<()> {
    debug!("[index_transaction] Indexing...");
    let idx = ctx.tx_idx;
    let is_evm = check_if_evm_tx(&ctx.tx);

    let txid = ctx.tx.txid;
    let vin_count = ctx.tx.vin.len();
    let vout_count = ctx.tx.vout.len();

    let mut total_vout_value = Decimal::zero();
    let mut vouts = Vec::with_capacity(vout_count);
    // Index transaction vout
    for vout in ctx.tx.vout.clone().into_iter() {
        let tx_vout = TransactionVout {
            id: format!("{}{:x}", txid, vout.n),
            txid,
            n: vout.n,
            value: vout.value.to_string(),
            token_id: Some(0),
            script: TransactionVoutScript {
                hex: vout.script_pub_key.hex,
                r#type: vout.script_pub_key.r#type,
            },
        };
        services
            .transaction
            .vout_by_id
            .put(&(txid, vout.n), &tx_vout)?;

        total_vout_value += Decimal::from_f64(vout.value).ok_or(Error::DecimalConversionError)?;
        vouts.push(tx_vout);
    }

    // Indexing transaction vin
    for vin in ctx.tx.vin.clone().into_iter() {
        if is_evm {
            continue;
        }

        let vin = TransactionVin::from_vin_and_txid(vin, txid, &vouts);
        services.transaction.vin_by_id.put(&vin.id, &vin)?;
    }

    let order = idx;

    let tx = TransactionMapper {
        id: txid,
        txid,
        order,
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
    services
        .transaction
        .by_block_hash
        .put(&(ctx.block.hash, order), &txid)?;

    Ok(())
}
