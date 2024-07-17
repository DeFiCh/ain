mod auction;
pub mod loan_token;
mod masternode;
pub mod oracle;
pub mod oracle_test;
pub mod poolpair;
pub mod poolswap;
pub mod transaction;
pub mod tx_result;

pub mod helper;

use std::{collections::HashMap, sync::Arc, time::Instant};

use ain_dftx::{deserialize, is_skipped_tx, DfTx, Stack};
use defichain_rpc::json::blockchain::{Block, Transaction, Vin, VinStandard};
use helper::check_if_evm_tx;
use loan_token::invalidate_block_end;
use log::debug;
pub use poolswap::{PoolCreationHeight, PoolSwapAggregatedInterval, AGGREGATED_INTERVALS};
use rust_decimal::{prelude::FromPrimitive, Decimal};

use crate::{
    error::IndexAction,
    hex_encoder::as_sha256,
    index_transaction,
    model::{
        Block as BlockMapper, BlockContext, PoolSwapAggregated, PoolSwapAggregatedAggregated,
        ScriptActivity, ScriptActivityScript, ScriptActivityType, ScriptActivityTypeHex,
        ScriptActivityVin, ScriptActivityVout, ScriptAggregation, ScriptAggregationAmount,
        ScriptAggregationScript, ScriptAggregationStatistic, ScriptUnspent, ScriptUnspentScript,
        ScriptUnspentVout, TransactionVout, TransactionVoutScript,
    },
    repository::{RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Error, Result, Services,
};
pub(crate) trait Index {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()>;

    // TODO: allow dead_code at the moment
    #[allow(dead_code)]
    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()>;
}

#[derive(Debug)]
pub struct Context {
    block: BlockContext,
    tx: Transaction,
    tx_idx: usize,
}

fn log_elapsed(previous: Instant, msg: &str) {
    let now = Instant::now();
    debug!("{} in {} ms", msg, now.duration_since(previous).as_millis());
}

fn get_bucket(block: &Block<Transaction>, interval: i64) -> i64 {
    block.mediantime - (block.mediantime % interval)
}

fn index_block_start(services: &Arc<Services>, block: &Block<Transaction>) -> Result<()> {
    let pool_pairs = services
        .poolpair
        .by_height
        .list(None, SortOrder::Descending)?
        .map(|el| {
            let ((k, _), (pool_id, id_token_a, id_token_b)) = el?;
            Ok(PoolCreationHeight {
                id: pool_id,
                id_token_a,
                id_token_b,
                creation_height: k,
            })
        })
        .collect::<Result<Vec<_>>>()?;

    for interval in AGGREGATED_INTERVALS {
        for pool_pair in &pool_pairs {
            let repository = &services.pool_swap_aggregated;

            let prevs = repository
                .by_key
                .list(
                    Some((pool_pair.id, interval, i64::MAX)),
                    SortOrder::Descending,
                )?
                .take(1)
                .take_while(|item| match item {
                    Ok((k, _)) => k.0 == pool_pair.id && k.1 == interval,
                    _ => true,
                })
                .map(|e| repository.by_key.retrieve_primary_value(e))
                .collect::<Result<Vec<_>>>()?;

            let bucket = get_bucket(block, interval as i64);

            if prevs.len() == 1 && prevs[0].bucket >= bucket {
                break;
            }

            let aggregated = PoolSwapAggregated {
                id: format!("{}-{}-{}", pool_pair.id, interval, block.hash),
                key: format!("{}-{}", pool_pair.id, interval),
                bucket,
                aggregated: PoolSwapAggregatedAggregated {
                    amounts: Default::default(),
                },
                block: BlockContext {
                    hash: block.hash,
                    height: block.height,
                    time: block.time,
                    median_time: block.mediantime,
                },
            };

            let pool_swap_aggregated_key = (pool_pair.id, interval, bucket);
            let pool_swap_aggregated_id = (pool_pair.id, interval, block.hash);

            repository
                .by_key
                .put(&pool_swap_aggregated_key, &pool_swap_aggregated_id)?;
            repository
                .by_id
                .put(&pool_swap_aggregated_id, &aggregated)?;
        }
    }

    Ok(())
}

fn get_vin_standard(vin: &Vin) -> Option<VinStandard> {
    match vin {
        Vin::Coinbase(_vin) => None,
        Vin::Standard(vin) => Some(vin.clone()),
    }
}

fn find_tx_vout(
    services: &Arc<Services>,
    block: &Block<Transaction>,
    vin: &VinStandard,
) -> Result<Option<TransactionVout>> {
    let tx = block.tx.clone().into_iter().find(|tx| tx.txid == vin.txid);

    if let Some(tx) = tx {
        let vout = tx.vout.into_iter().find(|vout| vout.n == vin.vout);

        if let Some(vout) = vout {
            let value = Decimal::from_f64(vout.value).ok_or(Error::DecimalConversionError)?;
            let tx_vout = TransactionVout {
                id: format!("{}{:x}", tx.txid, vin.vout),
                txid: tx.txid,
                n: vout.n,
                value: format!("{:.8}", value),
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

fn index_script_activity(services: &Arc<Services>, block: &Block<Transaction>) -> Result<()> {
    for tx in block.tx.iter() {
        if check_if_evm_tx(tx) {
            continue;
        }

        for vin in tx.vin.iter() {
            let Some(vin) = get_vin_standard(vin) else {
                continue;
            };

            let Some(vout) = find_tx_vout(services, block, &vin)? else {
                log::error!("attempting to sync: {:?} but type: TransactionVout with id:{}-{} cannot be found in the index", IndexAction::Index, vin.txid, vin.vout);
                continue;
            };

            let hid = as_sha256(vout.script.hex.clone()); // as key
            let script_activity = ScriptActivity {
                id: format!(
                    "{}{}{}{}",
                    hex::encode(block.height.to_be_bytes()),
                    ScriptActivityTypeHex::Vin,
                    vin.txid,
                    hex::encode(vin.vout.to_be_bytes())
                ),
                hid: hid.clone(),
                r#type: ScriptActivityType::Vin,
                type_hex: ScriptActivityTypeHex::Vin,
                txid: tx.txid,
                block: BlockContext {
                    hash: block.hash,
                    height: block.height,
                    time: block.time,
                    median_time: block.mediantime,
                },
                script: ScriptActivityScript {
                    r#type: vout.script.r#type,
                    hex: vout.script.hex,
                },
                vin: Some(ScriptActivityVin {
                    txid: vin.txid,
                    n: vin.vout,
                }),
                vout: None,
                value: format!("{:.8}", vout.value.parse::<f32>()?),
                token_id: vout.token_id,
            };
            let id = (
                hid,
                block.height,
                ScriptActivityTypeHex::Vin,
                vin.txid,
                vin.vout,
            );
            services.script_activity.by_id.put(&id, &script_activity)?
        }

        for vout in tx.vout.iter() {
            if vout.script_pub_key.hex.starts_with(&[0x6a]) {
                continue;
            }
            let hid = as_sha256(vout.script_pub_key.hex.clone());
            let script_activity = ScriptActivity {
                id: format!(
                    "{}{}{}{}",
                    hex::encode(block.height.to_be_bytes()),
                    ScriptActivityTypeHex::Vout,
                    tx.txid,
                    hex::encode(vout.n.to_be_bytes())
                ),
                hid: hid.clone(),
                r#type: ScriptActivityType::Vout,
                type_hex: ScriptActivityTypeHex::Vout,
                txid: tx.txid,
                block: BlockContext {
                    hash: block.hash,
                    height: block.height,
                    time: block.time,
                    median_time: block.mediantime,
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
                value: format!("{:.8}", vout.value),
                token_id: vout.token_id,
            };
            let id = (
                hid,
                block.height,
                ScriptActivityTypeHex::Vout,
                tx.txid,
                vout.n,
            );
            services.script_activity.by_id.put(&id, &script_activity)?
        }
    }

    Ok(())
}

fn index_script_aggregation(services: &Arc<Services>, block: &Block<Transaction>) -> Result<()> {
    let mut record: HashMap<String, ScriptAggregation> = HashMap::new();

    fn find_script_aggregation(
        record: &mut HashMap<String, ScriptAggregation>,
        block: &Block<Transaction>,
        hex: Vec<u8>,
        script_type: String,
    ) -> ScriptAggregation {
        let hid = as_sha256(hex.clone());
        let aggregation = record.get(&hid).cloned();

        if let Some(aggregation) = aggregation {
            aggregation
        } else {
            let aggregation = ScriptAggregation {
                id: (hid.clone(), block.height),
                hid: hid.clone(),
                block: BlockContext {
                    hash: block.hash,
                    height: block.height,
                    median_time: block.mediantime,
                    time: block.time,
                },
                script: ScriptAggregationScript {
                    r#type: script_type,
                    hex,
                },
                statistic: ScriptAggregationStatistic {
                    tx_count: 0,
                    tx_in_count: 0,
                    tx_out_count: 0,
                },
                amount: ScriptAggregationAmount {
                    tx_in: 0.0,
                    tx_out: 0.0,
                    unspent: 0.0,
                },
            };
            record.insert(hid, aggregation.clone());
            aggregation
        }
    }

    for tx in block.tx.iter() {
        if check_if_evm_tx(tx) {
            continue;
        }

        for vin in tx.vin.iter() {
            let Some(vin) = get_vin_standard(vin) else {
                continue;
            };

            let Some(vout) = find_tx_vout(services, block, &vin)? else {
                log::error!("attempting to sync: {:?} but type: TransactionVout with id:{}-{} cannot be found in the index", IndexAction::Index, vin.txid, vin.vout);
                continue;
            };

            // SPENT (REMOVE)
            let mut aggregation =
                find_script_aggregation(&mut record, block, vout.script.hex, vout.script.r#type);
            aggregation.statistic.tx_out_count += 1;
            aggregation.amount.tx_out += vout.value.parse::<f64>()?;
            record.insert(aggregation.hid.clone(), aggregation);
        }

        for vout in tx.vout.iter() {
            if vout.script_pub_key.hex.starts_with(&[0x6a]) {
                continue;
            }

            // Unspent (ADD)
            let mut aggregation = find_script_aggregation(
                &mut record,
                block,
                vout.script_pub_key.hex.clone(),
                vout.script_pub_key.r#type.clone(),
            );
            aggregation.statistic.tx_in_count += 1;
            aggregation.amount.tx_in += vout.value;
            record.insert(aggregation.hid.clone(), aggregation);
        }
    }

    for (_, mut aggregation) in record.clone().into_iter() {
        let repo = &services.script_aggregation;
        let latest = repo
            .by_id
            .list(
                Some((aggregation.hid.clone(), u32::MAX)),
                SortOrder::Descending,
            )?
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
        record.insert(aggregation.hid.clone(), aggregation.clone());

        repo.by_id
            .put(&(aggregation.hid.clone(), block.height), &aggregation)?;
    }
    Ok(())
}

fn index_script_unspent(services: &Arc<Services>, block: &Block<Transaction>) -> Result<()> {
    for tx in block.tx.iter() {
        if check_if_evm_tx(tx) {
            continue;
        }

        for vin in tx.vin.iter() {
            let Some(vin) = get_vin_standard(vin) else {
                continue;
            };

            let key = (block.height, vin.txid, vin.vout);
            let id = services.script_unspent.by_key.get(&key)?;
            if let Some(id) = id {
                services.script_unspent.by_id.delete(&id)?;
                services.script_unspent.by_key.delete(&key)?
            }
        }

        for vout in tx.vout.iter() {
            let hid = as_sha256(vout.script_pub_key.hex.clone());
            let script_unspent = ScriptUnspent {
                id: format!("{}{}", tx.txid, hex::encode(vout.n.to_be_bytes())),
                hid: hid.clone(),
                sort: format!(
                    "{}{}{}",
                    hex::encode(block.height.to_be_bytes()),
                    tx.txid,
                    hex::encode(vout.n.to_be_bytes())
                ),
                block: BlockContext {
                    hash: block.hash,
                    height: block.height,
                    median_time: block.mediantime,
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

            let id = (
                hid.clone(),
                hex::encode(block.height.to_be_bytes()),
                tx.txid,
                hex::encode(vout.n.to_be_bytes()),
            );
            let key = (block.height, tx.txid, vout.n);
            services.script_unspent.by_key.put(&key, &id)?;
            services.script_unspent.by_id.put(&id, &script_unspent)?
        }
    }

    Ok(())
}

fn index_block_end(services: &Arc<Services>, block: &BlockContext) -> Result<()> {
    loan_token::index_active_price(services, block)?;
    Ok(())
}

pub fn index_block(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    debug!("[index_block] Indexing block...");
    let start = Instant::now();
    let block_hash = block.hash;
    let transaction_count = block.tx.len();
    let block_ctx = BlockContext {
        height: block.height,
        hash: block_hash,
        time: block.time,
        median_time: block.mediantime,
    };

    // dftx
    index_block_start(services, &block)?;

    index_script_activity(services, &block)?;

    index_script_aggregation(services, &block)?;

    index_script_unspent(services, &block)?;

    for (tx_idx, tx) in block.tx.clone().into_iter().enumerate() {
        if is_skipped_tx(&tx.txid) {
            continue;
        }
        let start = Instant::now();
        let ctx = Context {
            block: block_ctx.clone(),
            tx,
            tx_idx,
        };

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
            Err(bitcoin::consensus::encode::Error::ParseFailed("Invalid marker")) => {
                println!("Discarding invalid marker");
            }
            Err(e) => return Err(e.into()),
            Ok(Stack { dftx, .. }) => {
                match dftx {
                    DfTx::CreateMasternode(data) => data.index(services, &ctx)?,
                    DfTx::UpdateMasternode(data) => data.index(services, &ctx)?,
                    DfTx::ResignMasternode(data) => data.index(services, &ctx)?,
                    DfTx::AppointOracle(data) => data.index(services, &ctx)?,
                    DfTx::RemoveOracle(data) => data.index(services, &ctx)?,
                    DfTx::UpdateOracle(data) => data.index(services, &ctx)?,
                    DfTx::SetOracleData(data) => data.index(services, &ctx)?,
                    DfTx::PoolSwap(data) => data.index(services, &ctx)?,
                    DfTx::SetLoanToken(data) => data.index(services, &ctx)?,
                    DfTx::CompositeSwap(data) => data.index(services, &ctx)?,
                    DfTx::CreatePoolPair(data) => data.index(services, &ctx)?,
                    DfTx::PlaceAuctionBid(data) => data.index(services, &ctx)?,
                    _ => (),
                }
                log_elapsed(start, "Indexed dftx");
            }
        }
    }

    log_elapsed(start, "Indexed block");

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
        stake_modifier: block.stake_modifier.to_owned(),
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

    //index block end
    index_block_end(services, &block_ctx)?;

    Ok(())
}

pub fn invalidate_block(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    let block_hash = block.hash;
    let block_ctx = BlockContext {
        height: block.height,
        hash: block_hash,
        time: block.time,
        median_time: block.mediantime,
    };
    invalidate_block_end(services, block.clone())?;
    for i in 0..block.tx.len() {
        let txn = &block.tx[i];
        let ctx = Context {
            block: block_ctx.clone(),
            tx:txn.clone(),
            tx_idx:i,
        };

        for vout in &txn.vout {
            if !vout.script_pub_key.asm.starts_with("OP_RETURN 44665478") {
                continue;
            }
            let bytes = hex::decode(vout.script_pub_key.hex.clone())?;
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
            let _tx = deserialize::<Stack>(raw_tx)?;
            match deserialize::<Stack>(raw_tx) {
                Err(bitcoin::consensus::encode::Error::ParseFailed("Invalid marker")) => {
                    println!("Discarding invalid marker");
                }
                Err(e) => return Err(e.into()),
                Ok(Stack { dftx, .. }) => {
                    match dftx {
                        DfTx::CreateMasternode(data) => data.invalidate(services, &ctx)?,
                        DfTx::UpdateMasternode(data) => data.invalidate(services, &ctx)?,
                        DfTx::ResignMasternode(data) => data.invalidate(services, &ctx)?,
                        DfTx::AppointOracle(data) => data.invalidate(services, &ctx)?,
                        DfTx::RemoveOracle(data) => data.invalidate(services, &ctx)?,
                        DfTx::UpdateOracle(data) => data.invalidate(services, &ctx)?,
                        DfTx::SetOracleData(data) => data.invalidate(services, &ctx)?,
                        DfTx::PoolSwap(data) => data.invalidate(services, &ctx)?,
                        DfTx::SetLoanToken(data) => data.invalidate(services, &ctx)?,
                        DfTx::CompositeSwap(data) => data.invalidate(services, &ctx)?,
                        DfTx::CreatePoolPair(data) => data.invalidate(services, &ctx)?,
                       
                        _ => (),
                    }
                    
                }
            }
        }
    }
    invalidate_block_start(services, block)?;
    Ok(())
}

pub fn invalidate_block_start(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    services
        .masternode
        .by_height
        .delete(&(block.height, block.tx[0].txid))?;

    let pool_pairs: Result<Vec<u32>> = services
        .poolpair
        .by_height
        .list(Some((block.height, usize::MAX)), SortOrder::Descending)?
        .map(|el| {
            let ((k, _), (_pool_id, _id_token_a, _id_token_b)) = el?;
            Ok(k)
        })
        .collect();

    let poolpairs = pool_pairs?;
    for pool_id in poolpairs {
        for interval in AGGREGATED_INTERVALS {
            services
                .pool_swap_aggregated
                .by_id
                .delete(&(pool_id, interval, block.hash))?;
        }
    }

    Ok(())
}
