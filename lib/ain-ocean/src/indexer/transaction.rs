use std::str::FromStr;

use bitcoin::Txid;
use dftx_rs::Transaction;
use log::debug;

use super::BlockContext;
use crate::{
    indexer::Result, model::Transaction as TrasnactionMapper, repository::RepositoryOps, SERVICES,
};

pub struct TransactionIndex {
    pub id: String,
    pub order: i32,
    pub block: BlockContext,
    pub txid: String,
    pub hash: String,
    pub version: i32,
    pub size: i32,
    pub v_size: i32,
    pub weight: i32,
    pub total_vout_value: String,
    pub lock_time: i32,
    pub vin_count: i32,
    pub vout_count: i32,
}

pub fn index_transaction(ctx: &BlockContext, tx: TransactionIndex) -> Result<()> {
    debug!("[CreateTransaction] Indexing...");
    let tx_id = Txid::from_str(&tx.txid)?;
    let trx = TrasnactionMapper {
        id: tx_id.to_string(),
        order: tx.order,
        block: ctx.clone(),
        txid: tx.txid,
        hash: ctx.hash.to_string(),
        version: tx.version,
        size: tx.size,
        v_size: tx.v_size,
        weight: tx.weight,
        total_vout_value: tx.total_vout_value,
        lock_time: tx.lock_time,
        vin_count: tx.vin_count,
        vout_count: tx.vout_count,
    };
    SERVICES.transaction.by_id.put(&tx_id, &trx)?;
    SERVICES.transaction.by_block_hash.put(&ctx.hash, &trx)?;

    Ok(())
}

pub fn invalidate(ctx: &BlockContext, tx: Txid, idx: usize) -> Result<()> {
    debug!("[CreateMasternode] Invalidating...");
    SERVICES.transaction.by_id.delete(&tx)?;
    SERVICES.transaction.by_block_hash.delete(&ctx.hash)?;
    Ok(())
}
