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

use std::{sync::Arc, time::Instant};

use ain_dftx::{deserialize, is_skipped_tx, DfTx, Stack};
use defichain_rpc::json::blockchain::{Block, Transaction, Vin, VinStandard};
use helper::check_if_evm_tx;
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
        ScriptActivityVin, ScriptActivityVout, ScriptUnspent, ScriptUnspentScript, ScriptUnspentVout, TransactionVout, TransactionVoutScript
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

fn index_script_activity(services: &Arc<Services>, block: &Block<Transaction>) -> Result<()> {
    for tx in block.tx.iter() {
        if check_if_evm_tx(tx) {
            continue;
        }

        for vin in tx.vin.iter() {
            let vin_standard = get_vin_standard(vin);
            if vin_standard.is_none() {
                continue;
            }
            let vin = vin_standard.unwrap();
            let tx_vout = if tx.txid == vin.txid {
                let vout = tx.vout.iter().find(|vout| vout.n == vin.vout);
                if let Some(vout) = vout {
                    let value =
                        Decimal::from_f64(vout.value).ok_or(Error::DecimalConversionError)?;
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
                    Some(tx_vout)
                } else {
                    None
                }
            } else {
                None
            };

            let vout = if let Some(tx_vout) = tx_vout {
                tx_vout
            } else {
                let tx_vout = services.transaction.vout_by_id.get(&(vin.txid, vin.vout))?;
                if tx_vout.is_none() {
                    return Err(Error::NotFoundIndex(
                        IndexAction::Index,
                        "TransactionVout".to_string(),
                        format!("{}-{}", vin.txid, vin.vout),
                    ));
                }
                tx_vout.unwrap()
            };

            let id = (block.height, ScriptActivityType::Vin, vin.txid, vin.vout);
            let hid = as_sha256(vout.script.hex.clone()); // as key
            let script_activity = ScriptActivity {
                id: id.clone(),
                hid: hid.clone(),
                r#type: ScriptActivityType::Vin,
                type_hex: ScriptActivityTypeHex::Vout,
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
                value: vout.value,
                token_id: vout.token_id,
            };
            services.script_activity.by_key.put(&hid, &id)?;
            services.script_activity.by_id.put(&id, &script_activity)?
        }

        for vout in tx.vout.iter() {
            if vout.script_pub_key.hex.starts_with(&[0x6a]) {
                continue;
            }
            let id = (block.height, ScriptActivityType::Vout, tx.txid, vout.n);
            let hid = as_sha256(vout.script_pub_key.hex.clone());
            let script_activity = ScriptActivity {
                id: id.clone(),
                hid: hid.clone(),
                r#type: ScriptActivityType::Vin,
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
                value: vout.value.to_string(),
                token_id: vout.token_id,
            };
            services.script_activity.by_key.put(&hid, &id)?;
            services.script_activity.by_id.put(&id, &script_activity)?
        }
    }

    Ok(())
}

fn index_script_unspent(services: &Arc<Services>, block: &Block<Transaction>) -> Result<()> {
    for tx in block.tx.iter() {
        if check_if_evm_tx(tx) {
            continue
        }

        for vin in tx.vin.iter() {
            let vin_standard = get_vin_standard(vin);
            if vin_standard.is_none() {
                continue;
            }
            let vin = vin_standard.unwrap();
            let id = (vin.txid, vin.vout);
            services.script_unspent.by_id.delete(&id)?
        }

        for vout in tx.vout.iter() {
            let id = (tx.txid, vout.n);
            let hid = as_sha256(vout.script_pub_key.hex.clone());
            let script_unspent = ScriptUnspent {
                id,
                hid,
                sort: format!("{:x}{}{:x}", block.height, tx.txid, vout.n),
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
                    value: vout.value.to_string(),
                    token_id: vout.token_id,
                }
            };
            services.script_unspent.by_id.put(&id, &script_unspent)?
        }
    }

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

        index_script_activity(services, &block)?;

        // index_script_aggregation

        index_script_unspent(services, &block)?;

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
                    // DfTx::PlaceAuctionBid(data) => data.index(services, &ctx)?,
                    _ => (),
                }
                log_elapsed(start, "Indexed dftx");
            }
        }

        index_transaction(services, ctx)?;
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

    Ok(())
}

pub fn invalidate_block(_block: Block<Transaction>) -> Result<()> {
    Ok(())
}
