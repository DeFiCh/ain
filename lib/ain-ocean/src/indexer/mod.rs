mod auction;
mod masternode;
pub mod oracle;
pub mod oracle_test;
mod pool;
mod pool_test;
pub mod transaction;
pub mod tx_result;

use std::{sync::Arc, time::Instant};

use ain_dftx::{deserialize, DfTx, Stack};
use defichain_rpc::json::blockchain::{Block, Transaction};
use log::debug;

pub use pool::AGGREGATED_INTERVALS;

use crate::{
    index_transaction,
    model::{
        Block as BlockMapper, BlockContext, PoolSwapAggregated, PoolSwapAggregatedAggregated,
    },
    repository::{RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Result, Services,
};

pub(crate) trait Index {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()>;

    // TODO: allow dead_code at the moment
    #[allow(dead_code)]
    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()>;
}

#[derive(Debug, Clone)]
pub struct PoolCreationHeight {
    pub id: u32,
    pub creation_height: u32,
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

fn index_block_start(
    services: &Arc<Services>,
    block: &Block<Transaction>,
    pool_pairs: Vec<PoolCreationHeight>,
) -> Result<()> {
    debug!("[index_block_start] pool_pairs: {:?}", pool_pairs);

    let mut pool_pairs = pool_pairs;
    pool_pairs.sort_by(|a, b| b.creation_height.cmp(&a.creation_height));

    for interval in AGGREGATED_INTERVALS {
        for pool_pair in &pool_pairs {
            let repository = &services.pool_swap_aggregated;

            // let prevs = repository
            //     .by_key
            //     .list(Some((pool_pair.id, interval, i64::MAX)), SortOrder::Descending)?
            //     .take(1)
            //     .take_while(|item| match item {
            //         Ok((k, _)) => k.0 == pool_pair.id && k.1 == interval,
            //         _ => true,
            //     })
            //     .map(|e| repository.by_key.retrieve_primary_value(e))
            //     .collect::<Result<Vec<_>>>()?;

            let bucket = get_bucket(block, interval as i64);

            // debug!("index_block_start prevs: {:?}", prevs);
            // debug!("index_block_start bucket: {:?}", bucket);
            // if prevs.len() == 1 && prevs[0].bucket >= bucket {
            //     break;
            // }

            let aggregated = PoolSwapAggregated {
                id: format!("{}-{}-{}", pool_pair.id, interval, block.hash),
                key: format!("{}-{}", pool_pair.id, interval),
                bucket,
                // aggregated: PoolSwapAggregatedAggregated {
                //     amounts: Default::default(),
                // },
                block: BlockContext {
                    hash: block.hash,
                    height: block.height,
                    time: block.time,
                    median_time: block.mediantime,
                },
            };

            let pool_swap_aggregated_key = (pool_pair.id, interval, bucket);
            let pool_swap_aggregated_id = (pool_pair.id, interval, block.hash);
            debug!("index_block_start pool_swap_aggregated_key: {:?}, pool_swap_aggregated_id: {:?}, interval: {:?}", pool_swap_aggregated_key, pool_swap_aggregated_id, interval);

            repository.by_key.put(&pool_swap_aggregated_key, &pool_swap_aggregated_id)?;
            repository.by_id.put(&pool_swap_aggregated_id, &aggregated)?;
        }
    }

    Ok(())
}

pub fn index_block(
    services: &Arc<Services>,
    block: Block<Transaction>,
    pools: Vec<PoolCreationHeight>,
) -> Result<()> {
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

    index_block_start(services, &block, pools)?;

    for (tx_idx, tx) in block.tx.into_iter().enumerate() {
        let start = Instant::now();
        let ctx = Context {
            block: block_ctx.clone(),
            tx,
            tx_idx,
        };

        let bytes = &ctx.tx.vout[0].script_pub_key.hex;
        if bytes.len() > 6 && bytes[0] == 0x6a && bytes[1] <= 0x4e {
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
                        // DfTx::CompositeSwap(data) => data.index(services, &ctx)?,
                        // DfTx::PlaceAuctionBid(data) => data.index(services, &ctx)?,
                        _ => (),
                    }
                    log_elapsed(start, "Indexed dftx");
                }
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
