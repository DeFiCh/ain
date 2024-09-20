use std::{sync::Arc, time::Instant};

use defichain_rpc::json::blockchain::Vin;
use log::trace;
use rust_decimal::{
    prelude::{FromPrimitive, Zero},
    Decimal,
};
use snafu::OptionExt;

use super::{helper::check_if_evm_tx, Context};
use crate::{
    error::DecimalConversionSnafu,
    indexer::{log_elapsed, Result},
    model::{
        Transaction as TransactionMapper, TransactionVin, TransactionVinType, TransactionVout,
        TransactionVoutScript,
    },
    storage::RepositoryOps,
    Services,
};

pub fn index_transaction(services: &Arc<Services>, ctx: &Context) -> Result<()> {
    trace!("[index_transaction] Indexing...");
    let start = Instant::now();

    let idx = ctx.tx_idx;
    let is_evm = check_if_evm_tx(&ctx.tx);

    let txid = ctx.tx.txid;
    let vin_count = ctx.tx.vin.len();
    let vout_count = ctx.tx.vout.len();

    let mut total_vout_value = Decimal::zero();
    let mut vouts = Vec::with_capacity(vout_count);

    // Index transaction vout
    let start_vout = Instant::now();
    for vout in ctx.tx.vout.clone() {
        let tx_vout = TransactionVout {
            vout: vout.n,
            txid,
            n: vout.n,
            value: vout.value,
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

        total_vout_value += Decimal::from_f64(vout.value).context(DecimalConversionSnafu)?;
        vouts.push(tx_vout);
    }
    log_elapsed(start_vout, format!("Indexed vouts"));

    // Indexing transaction vin
    let start_vin = Instant::now();
    for vin in ctx.tx.vin.clone() {
        if is_evm {
            continue;
        }

        let vin = TransactionVin::from_vin_and_txid(vin, txid, &vouts);

        match &vin.r#type {
            TransactionVinType::Coinbase(_) => {
                let vin_id = format!("{}00", ctx.tx.txid);
                services.transaction.vin_by_id.put(&vin_id, &vin)?;
            }
            TransactionVinType::Standard((txid, vout)) => {
                let vin_id = format!("{}{}{:x}", ctx.tx.txid, txid, vout);
                services.transaction.vin_by_id.put(&vin_id, &vin)?;
            }
        }
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
    log_elapsed(start_vin, format!("Indexed vins"));

    // Index transaction
    services.transaction.by_id.put(&txid, &tx)?;
    services
        .transaction
        .by_block_hash
        .put(&(ctx.block.hash, order), &txid)?;

    log_elapsed(start, format!("Indexed transaction {:x}", txid));
    Ok(())
}

pub fn invalidate_transaction(services: &Arc<Services>, ctx: &Context) -> Result<()> {
    services
        .transaction
        .by_block_hash
        .delete(&(ctx.block.hash, ctx.tx_idx))?;
    services.transaction.by_id.delete(&ctx.tx.txid)?;

    let is_evm = check_if_evm_tx(&ctx.tx);
    for vin in ctx.tx.vin.clone().into_iter() {
        if is_evm {
            continue;
        }
        match vin {
            Vin::Coinbase(_) => {
                let vin_id = format!("{}00", ctx.tx.txid);
                services.transaction.vin_by_id.delete(&vin_id)?
            }
            Vin::Standard(vin) => {
                let vin_id = format!("{}{}{:x}", ctx.tx.txid, vin.txid, vin.vout);
                services.transaction.vin_by_id.delete(&vin_id)?
            }
        }
    }

    for vout in ctx.tx.vout.clone().into_iter() {
        services
            .transaction
            .vout_by_id
            .delete(&(ctx.tx.txid, vout.n))?
    }

    Ok(())
}
