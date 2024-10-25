mod auction;
pub mod loan_token;
mod masternode;
pub mod oracle;
pub mod poolswap;
pub mod transaction;
pub mod tx_result;

pub mod helper;

use std::{
    collections::{BTreeMap, HashSet},
    sync::Arc,
    time::Instant,
};

use ain_dftx::{deserialize, is_skipped_tx, DfTx, Stack};
use defichain_rpc::json::blockchain::{Block, Transaction, Vin, VinStandard, Vout};
use helper::check_if_evm_tx;
use log::trace;
pub use poolswap::PoolSwapAggregatedInterval;

use crate::{
    error::{Error, IndexAction},
    hex_encoder::as_sha256,
    index_transaction, invalidate_transaction,
    model::{
        Block as BlockMapper, BlockContext, ScriptActivity, ScriptActivityScript,
        ScriptActivityType, ScriptActivityTypeHex, ScriptActivityVin, ScriptActivityVout,
        ScriptAggregation, ScriptAggregationAmount, ScriptAggregationScript,
        ScriptAggregationStatistic, ScriptUnspent, ScriptUnspentScript, ScriptUnspentVout,
        TransactionVout, TransactionVoutScript,
    },
    storage::{RepositoryOps, SortOrder},
    Result, Services,
};

pub trait Index {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()>;

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()>;
}

pub trait IndexBlockStart: Index {
    fn index_block_start(self, services: &Arc<Services>, block: &BlockContext) -> Result<()>;

    fn invalidate_block_start(self, services: &Arc<Services>, block: &BlockContext) -> Result<()>;
}

pub trait IndexBlockEnd: Index {
    fn index_block_end(self, services: &Arc<Services>, block: &BlockContext) -> Result<()>;

    fn invalidate_block_end(self, services: &Arc<Services>, block: &BlockContext) -> Result<()>;
}

#[derive(Debug)]
pub struct Context {
    block: BlockContext,
    tx: Transaction,
    tx_idx: usize,
}

fn log_elapsed<S: AsRef<str> + std::fmt::Display>(previous: Instant, msg: S) {
    let now = Instant::now();
    trace!("{} in {} ms", msg, now.duration_since(previous).as_millis());
}

fn get_vin_standard(vin: &Vin) -> Option<VinStandard> {
    match vin {
        Vin::Coinbase(_vin) => None,
        Vin::Standard(vin) => Some(vin.clone()),
    }
}

fn find_tx_vout(
    services: &Arc<Services>,
    vin: &VinStandard,
    txs: &[Transaction],
) -> Result<Option<TransactionVout>> {
    let tx = txs.iter().find(|tx| tx.txid == vin.txid);

    if let Some(tx) = tx {
        let vout = tx.vout.iter().find(|vout| vout.n == vin.vout);

        if let Some(vout) = vout {
            let tx_vout = TransactionVout {
                vout: vin.vout,
                txid: tx.txid,
                n: vout.n,
                value: vout.value,
                token_id: vout.token_id,
                script: TransactionVoutScript {
                    r#type: vout.script_pub_key.r#type.clone(),
                    hex: vout.script_pub_key.hex.clone(),
                },
            };
            return Ok(Some(tx_vout));
        }
    }
    services.transaction.vout_by_id.get(&(vin.txid, vin.vout))
}

fn index_script_activity_vin(
    services: &Arc<Services>,
    vin: &VinStandard,
    vout: &TransactionVout,
    ctx: &Context,
) -> Result<()> {
    let tx = &ctx.tx;
    let block = &ctx.block;

    let hid = as_sha256(&vout.script.hex); // as key
    let script_activity = ScriptActivity {
        hid,
        r#type: ScriptActivityType::Vin,
        type_hex: ScriptActivityTypeHex::Vin,
        txid: tx.txid,
        block: BlockContext {
            hash: block.hash,
            height: block.height,
            time: block.time,
            median_time: block.median_time,
        },
        script: ScriptActivityScript {
            r#type: vout.script.r#type.clone(),
            hex: vout.script.hex.clone(),
        },
        vin: Some(ScriptActivityVin {
            txid: vin.txid,
            n: vin.vout,
        }),
        vout: None,
        value: vout.value,
        token_id: vout.token_id,
    };
    let id = (
        hid,
        block.height.to_be_bytes(),
        ScriptActivityTypeHex::Vin,
        vin.txid,
        vin.vout,
    );
    services.script_activity.by_id.put(&id, &script_activity)?;

    Ok(())
}

fn index_script_aggregation_vin(
    vout: &TransactionVout,
    block: &BlockContext,
    record: &mut BTreeMap<[u8; 32], ScriptAggregation>,
) {
    let hid = as_sha256(&vout.script.hex);
    let entry = record.entry(hid).or_insert_with(|| ScriptAggregation {
        hid,
        block: block.clone(),
        script: ScriptAggregationScript {
            r#type: vout.script.r#type.clone(),
            hex: vout.script.hex.clone(),
        },
        statistic: ScriptAggregationStatistic::default(),
        amount: ScriptAggregationAmount::default(),
    });
    entry.statistic.tx_out_count += 1;
    entry.amount.tx_out += vout.value;
}

fn index_script_unspent_vin(
    services: &Arc<Services>,
    vin: &VinStandard,
    ctx: &Context,
) -> Result<()> {
    let key = (ctx.block.height, vin.txid, vin.vout);
    let id = services.script_unspent.by_key.get(&key)?;
    if let Some(id) = id {
        services.script_unspent.by_id.delete(&id)?;
    }
    Ok(())
}

fn index_script_activity_vout(services: &Arc<Services>, vout: &Vout, ctx: &Context) -> Result<()> {
    let tx = &ctx.tx;
    let block = &ctx.block;

    let hid = as_sha256(&vout.script_pub_key.hex);
    let script_activity = ScriptActivity {
        hid,
        r#type: ScriptActivityType::Vout,
        type_hex: ScriptActivityTypeHex::Vout,
        txid: tx.txid,
        block: BlockContext {
            hash: block.hash,
            height: block.height,
            time: block.time,
            median_time: block.median_time,
        },
        script: ScriptActivityScript {
            r#type: vout.script_pub_key.r#type.clone(),
            hex: vout.script_pub_key.hex.clone(),
        },
        vin: None,
        vout: Some(ScriptActivityVout {
            txid: tx.txid,
            n: vout.n,
        }),
        value: vout.value,
        token_id: vout.token_id,
    };
    let id = (
        hid,
        block.height.to_be_bytes(),
        ScriptActivityTypeHex::Vout,
        tx.txid,
        vout.n,
    );
    services.script_activity.by_id.put(&id, &script_activity)?;
    Ok(())
}

fn index_script_aggregation_vout(
    vout: &Vout,
    block: &BlockContext,
    record: &mut BTreeMap<[u8; 32], ScriptAggregation>,
) {
    let hid = as_sha256(&vout.script_pub_key.hex);

    let entry = record.entry(hid).or_insert_with(|| ScriptAggregation {
        hid,
        block: block.clone(),
        script: ScriptAggregationScript {
            r#type: vout.script_pub_key.r#type.clone(),
            hex: vout.script_pub_key.hex.clone(),
        },
        statistic: ScriptAggregationStatistic::default(),
        amount: ScriptAggregationAmount::default(),
    });
    entry.statistic.tx_in_count += 1;
    entry.amount.tx_in += vout.value;
}

fn index_script_unspent_vout(services: &Arc<Services>, vout: &Vout, ctx: &Context) -> Result<()> {
    let tx = &ctx.tx;
    let block = &ctx.block;

    let hid = as_sha256(&vout.script_pub_key.hex);
    let script_unspent = ScriptUnspent {
        id: (tx.txid, vout.n.to_be_bytes()),
        hid,
        block: BlockContext {
            hash: block.hash,
            height: block.height,
            median_time: block.median_time,
            time: block.time,
        },
        script: ScriptUnspentScript {
            r#type: vout.script_pub_key.r#type.clone(),
            hex: vout.script_pub_key.hex.clone(),
        },
        vout: ScriptUnspentVout {
            txid: tx.txid,
            n: vout.n,
            value: vout.value,
            token_id: vout.token_id,
        },
    };

    let id = (hid, block.height.to_be_bytes(), tx.txid, vout.n);
    let key = (block.height, tx.txid, vout.n);
    services.script_unspent.by_key.put(&key, &id)?;
    services.script_unspent.by_id.put(&id, &script_unspent)?;
    Ok(())
}

fn index_script(services: &Arc<Services>, ctx: &Context, txs: &[Transaction]) -> Result<()> {
    trace!("[index_transaction] Indexing...");
    let start = Instant::now();

    let is_evm_tx = check_if_evm_tx(&ctx.tx);

    let mut record = BTreeMap::new();

    for vin in &ctx.tx.vin {
        if is_evm_tx {
            continue;
        }

        let Some(vin) = get_vin_standard(vin) else {
            continue;
        };

        index_script_unspent_vin(services, &vin, ctx)?;

        let Some(vout) = find_tx_vout(services, &vin, txs)? else {
            if is_skipped_tx(&vin.txid) {
                return Ok(());
            };

            return Err(Error::NotFoundIndex {
                action: IndexAction::Index,
                r#type: "Index script TransactionVout".to_string(),
                id: format!("{}-{}", vin.txid, vin.vout),
            });
        };

        index_script_activity_vin(services, &vin, &vout, ctx)?;

        // part of index_script_aggregation
        index_script_aggregation_vin(&vout, &ctx.block, &mut record);
    }

    for vout in &ctx.tx.vout {
        index_script_unspent_vout(services, vout, ctx)?;

        if vout.script_pub_key.hex.starts_with(&[0x6a]) {
            return Ok(());
        }

        index_script_activity_vout(services, vout, ctx)?;

        // part of index_script_aggregation
        index_script_aggregation_vout(vout, &ctx.block, &mut record);
    }

    // index_script_aggregation
    for (_, mut aggregation) in record.clone() {
        let repo = &services.script_aggregation;
        let latest = repo
            .by_id
            .list(Some((aggregation.hid, u32::MAX)), SortOrder::Descending)?
            .take(1)
            .take_while(|item| match item {
                Ok(((hid, _), _)) => &aggregation.hid == hid,
                _ => true,
            })
            .map(|item| {
                let (_, v) = item?;
                Ok(v)
            })
            .collect::<Result<Vec<_>>>()?;

        if let Some(latest) = latest.first().cloned() {
            aggregation.statistic.tx_in_count += latest.statistic.tx_in_count;
            aggregation.statistic.tx_out_count += latest.statistic.tx_out_count;

            aggregation.amount.tx_in += latest.amount.tx_in;
            aggregation.amount.tx_out += latest.amount.tx_out;
        }

        aggregation.statistic.tx_count =
            aggregation.statistic.tx_in_count + aggregation.statistic.tx_out_count;
        aggregation.amount.unspent = aggregation.amount.tx_in - aggregation.amount.tx_out;

        repo.by_id
            .put(&(aggregation.hid, ctx.block.height), &aggregation)?;

        record.insert(aggregation.hid, aggregation);
    }

    log_elapsed(start, format!("Indexed script {:x}", ctx.tx.txid));
    Ok(())
}

fn invalidate_script(services: &Arc<Services>, ctx: &Context, txs: &[Transaction]) -> Result<()> {
    let tx = &ctx.tx;
    let block = &ctx.block;

    let is_evm_tx = check_if_evm_tx(tx);

    let mut hid_set = HashSet::new();

    for vin in tx.vin.iter() {
        if is_evm_tx {
            continue;
        }

        let Some(vin) = get_vin_standard(vin) else {
            continue;
        };

        invalidate_script_unspent_vin(services, &ctx.tx, &vin)?;

        let Some(vout) = find_tx_vout(services, &vin, txs)? else {
            if is_skipped_tx(&vin.txid) {
                return Ok(());
            };

            return Err(Error::NotFoundIndex {
                action: IndexAction::Index,
                r#type: "Index script TransactionVout".to_string(),
                id: format!("{}-{}", vin.txid, vin.vout),
            });
        };

        invalidate_script_activity_vin(services, ctx.block.height, &vin, &vout)?;

        hid_set.insert(as_sha256(&vout.script.hex)); // part of invalidate_script_aggregation
    }

    for vout in tx.vout.iter() {
        invalidate_script_unspent_vout(services, ctx, vout)?;

        if vout.script_pub_key.hex.starts_with(&[0x6a]) {
            continue;
        }

        invalidate_script_activity_vout(services, ctx, vout)?;

        hid_set.insert(as_sha256(&vout.script_pub_key.hex)); // part of invalidate_script_aggregation
    }

    // invalidate_script_aggregation
    for hid in hid_set.into_iter() {
        services
            .script_aggregation
            .by_id
            .delete(&(hid, block.height))?
    }

    Ok(())
}

fn invalidate_script_unspent_vin(
    services: &Arc<Services>,
    tx: &Transaction,
    vin: &VinStandard,
) -> Result<()> {
    let Some(transaction) = services.transaction.by_id.get(&vin.txid)? else {
        return Err(Error::NotFoundIndex {
            action: IndexAction::Invalidate,
            r#type: "Transaction".to_string(),
            id: vin.txid.to_string(),
        });
    };

    let Some(vout) = services.transaction.vout_by_id.get(&(vin.txid, vin.vout))? else {
        return Err(Error::NotFoundIndex {
            action: IndexAction::Invalidate,
            r#type: "TransactionVout".to_string(),
            id: format!("{}{}", vin.txid, vin.vout),
        });
    };

    let hid = as_sha256(&vout.script.hex);

    let script_unspent = ScriptUnspent {
        id: (vout.txid, vout.n.to_be_bytes()),
        hid,
        block: BlockContext {
            hash: transaction.block.hash,
            height: transaction.block.height,
            median_time: transaction.block.median_time,
            time: transaction.block.time,
        },
        script: ScriptUnspentScript {
            r#type: vout.script.r#type,
            hex: vout.script.hex,
        },
        vout: ScriptUnspentVout {
            txid: tx.txid,
            n: vout.n,
            value: vout.value,
            token_id: vout.token_id,
        },
    };

    let id = (
        hid,
        transaction.block.height.to_be_bytes(),
        transaction.txid,
        vout.n,
    );
    let key = (transaction.block.height, transaction.txid, vout.n);

    services.script_unspent.by_key.put(&key, &id)?;
    services.script_unspent.by_id.put(&id, &script_unspent)?;

    Ok(())
}

fn invalidate_script_activity_vin(
    services: &Arc<Services>,
    height: u32,
    vin: &VinStandard,
    vout: &TransactionVout,
) -> Result<()> {
    let id = (
        as_sha256(&vout.script.hex),
        height.to_be_bytes(),
        ScriptActivityTypeHex::Vin,
        vin.txid,
        vin.vout,
    );
    services.script_activity.by_id.delete(&id)?;

    Ok(())
}

fn invalidate_script_unspent_vout(
    services: &Arc<Services>,
    ctx: &Context,
    vout: &Vout,
) -> Result<()> {
    let hid = as_sha256(&vout.script_pub_key.hex);
    let id = (hid, ctx.block.height.to_be_bytes(), ctx.tx.txid, vout.n);
    services.script_unspent.by_id.delete(&id)?;

    Ok(())
}

fn invalidate_script_activity_vout(
    services: &Arc<Services>,
    ctx: &Context,
    vout: &Vout,
) -> Result<()> {
    let id = (
        as_sha256(&vout.script_pub_key.hex),
        ctx.block.height.to_be_bytes(),
        ScriptActivityTypeHex::Vout,
        ctx.tx.txid,
        vout.n,
    );
    services.script_activity.by_id.delete(&id)?;
    Ok(())
}

pub fn index_block(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    trace!("[index_block] Indexing block...");
    let start = Instant::now();
    let block_hash = block.hash;
    let transaction_count = block.tx.len();
    let block_ctx = BlockContext {
        height: block.height,
        hash: block_hash,
        time: block.time,
        median_time: block.mediantime,
    };

    let mut dftxs = Vec::new();

    for (tx_idx, tx) in block.tx.clone().into_iter().enumerate() {
        if is_skipped_tx(&tx.txid) {
            continue;
        }

        let ctx = Context {
            block: block_ctx.clone(),
            tx,
            tx_idx,
        };

        index_script(services, &ctx, &block.tx)?;

        index_transaction(services, &ctx)?;

        let bytes = &ctx.tx.vout[0].script_pub_key.hex;
        if bytes.len() <= 6 || bytes[0] != 0x6a || bytes[1] > 0x4e {
            continue;
        }

        let offset = 1 + match bytes[1] {
            0x4c => 2,
            0x4d => 3,
            0x4e => 4,
            _ => 1,
        };
        let raw_tx = &bytes[offset..];
        match deserialize::<Stack>(raw_tx) {
            Err(bitcoin::consensus::encode::Error::ParseFailed("Invalid marker")) => (),
            Err(e) => return Err(e.into()),
            Ok(Stack { dftx, .. }) => dftxs.push((dftx, ctx)),
        }
    }

    // index_block_start
    for (dftx, _) in &dftxs {
        if let DfTx::PoolSwap(data) = dftx.clone() {
            data.index_block_start(services, &block_ctx)?
        }
    }

    // index_dftx
    for (dftx, ctx) in &dftxs {
        let start = Instant::now();

        match dftx.clone() {
            DfTx::CreateMasternode(data) => data.index(services, ctx)?,
            DfTx::UpdateMasternode(data) => data.index(services, ctx)?,
            DfTx::ResignMasternode(data) => data.index(services, ctx)?,
            DfTx::AppointOracle(data) => data.index(services, ctx)?,
            DfTx::RemoveOracle(data) => data.index(services, ctx)?,
            DfTx::UpdateOracle(data) => data.index(services, ctx)?,
            DfTx::SetOracleData(data) => data.index(services, ctx)?,
            DfTx::PoolSwap(data) => data.index(services, ctx)?,
            DfTx::SetLoanToken(data) => data.index(services, ctx)?,
            DfTx::CompositeSwap(data) => data.index(services, ctx)?,
            DfTx::PlaceAuctionBid(data) => data.index(services, ctx)?,
            _ => (),
        }
        log_elapsed(start, "Indexed dftx");
    }

    // index_block_end
    for (dftx, _) in dftxs {
        if let DfTx::SetLoanToken(data) = dftx {
            data.index_block_end(services, &block_ctx)?
        }
    }

    let block_mapper = BlockMapper {
        hash: block_hash,
        id: block_hash,
        previous_hash: block.previousblockhash,
        height: block.height,
        version: block.version,
        time: block.time,
        median_time: block.mediantime,
        transaction_count,
        difficulty: block.difficulty,
        masternode: block.masternode,
        minter: block.minter,
        minter_block_count: block.minted_blocks,
        stake_modifier: block.stake_modifier.clone(),
        merkleroot: block.merkleroot,
        size: block.size,
        size_stripped: block.strippedsize,
        weight: block.weight,
    };

    // services.block.raw.put(&ctx.hash, &encoded_block)?; TODO
    services.block.by_id.put(&block_ctx.hash, &block_mapper)?;
    services
        .block
        .by_height
        .put(&block_ctx.height, &block_hash)?;

    log_elapsed(start, "Indexed block");

    Ok(())
}

pub fn invalidate_block(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    let block_ctx = BlockContext {
        height: block.height,
        hash: block.hash,
        time: block.time,
        median_time: block.mediantime,
    };

    let mut dftxs = Vec::new();

    for (tx_idx, tx) in block.tx.clone().into_iter().enumerate() {
        if is_skipped_tx(&tx.txid) {
            continue;
        }
        let ctx = Context {
            block: block_ctx.clone(),
            tx,
            tx_idx,
        };

        invalidate_script(services, &ctx, &block.tx)?;

        invalidate_transaction(services, &ctx)?;

        let bytes = &ctx.tx.vout[0].script_pub_key.hex;
        if bytes.len() <= 6 || bytes[0] != 0x6a || bytes[1] > 0x4e {
            continue;
        }

        let offset = 1 + match bytes[1] {
            0x4c => 2,
            0x4d => 3,
            0x4e => 4,
            _ => 1,
        };
        let raw_tx = &bytes[offset..];
        match deserialize::<Stack>(raw_tx) {
            Err(bitcoin::consensus::encode::Error::ParseFailed("Invalid marker")) => {
                println!("Discarding invalid marker");
            }
            Err(e) => return Err(e.into()),
            Ok(Stack { dftx, .. }) => dftxs.push((dftx, ctx)),
        }
    }

    // invalidate_block_end
    for (dftx, _) in &dftxs {
        if let DfTx::SetLoanToken(data) = dftx.clone() {
            data.invalidate_block_end(services, &block_ctx)?
        }
    }

    // invalidate_dftx
    for (dftx, ctx) in &dftxs {
        let start = Instant::now();
        match dftx {
            DfTx::CreateMasternode(data) => data.invalidate(services, ctx)?,
            DfTx::UpdateMasternode(data) => data.invalidate(services, ctx)?,
            DfTx::ResignMasternode(data) => data.invalidate(services, ctx)?,
            DfTx::AppointOracle(data) => data.invalidate(services, ctx)?,
            DfTx::RemoveOracle(data) => data.invalidate(services, ctx)?,
            DfTx::UpdateOracle(data) => data.invalidate(services, ctx)?,
            DfTx::SetOracleData(data) => data.invalidate(services, ctx)?,
            DfTx::PoolSwap(data) => data.invalidate(services, ctx)?,
            DfTx::SetLoanToken(data) => data.invalidate(services, ctx)?,
            DfTx::CompositeSwap(data) => data.invalidate(services, ctx)?,
            DfTx::PlaceAuctionBid(data) => data.invalidate(services, ctx)?,
            _ => (),
        }
        log_elapsed(start, "Invalidate dftx");
    }

    // invalidate_block_start
    for (dftx, _) in &dftxs {
        if let DfTx::PoolSwap(data) = dftx.clone() {
            data.invalidate_block_start(services, &block_ctx)?
        }
    }

    // invalidate_block
    services.block.by_height.delete(&block.height)?;
    services.block.by_id.delete(&block.hash)?;

    Ok(())
}
