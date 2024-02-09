mod auction;
mod masternode;
mod oracle;
mod pool;
pub mod transaction;
pub mod tx_result;

use std::{sync::Arc, time::Instant};

use defichain_rpc::json::blockchain::{Block, Transaction};
use dftx_rs::{deserialize, DfTx};
use log::debug;

use crate::{
    index_transaction,
    model::{Block as BlockMapper, BlockContext},
    repository::RepositoryOps,
    Result, Services,
};

pub(crate) trait Index {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()>;

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()>;
}

pub struct Context {
    block: BlockContext,
    tx: Transaction,
    tx_idx: usize,
}

fn log_elapsed(previous: Instant, msg: &str) {
    let now = Instant::now();
    debug!("{} in {} ms", msg, now.duration_since(previous).as_millis());
}

pub fn index_block(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    debug!("[index_block] Indexing block...");
    let start = Instant::now();

    let block_hash = block.hash;
    let block_ctx = BlockContext {
        height: block.height,
        hash: block_hash,
        time: block.time,
        median_time: block.mediantime,
    };

    let block_mapper = BlockMapper {
        hash: block_hash,
        previous_hash: block.previousblockhash,
        height: block.height,
        version: block.version,
        time: block.time,
        median_time: block.mediantime,
        transaction_count: block.tx.len(),
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

    for (tx_idx, tx) in block.tx.into_iter().enumerate() {
        let start = Instant::now();
        let ctx = Context {
            block: block_ctx.clone(),
            tx,
            tx_idx,
        };

        let bytes = &ctx.tx.vout[0].script_pub_key.hex;
        if bytes.len() > 2 && bytes[0] == 0x6a && bytes[1] <= 0x4e {
            let offset = 1 + match bytes[1] {
                0x4c => 2,
                0x4d => 3,
                0x4e => 4,
                _ => 1,
            };

            let raw_tx = &bytes[offset..];
            let dftx = deserialize::<DfTx>(raw_tx)?;
            debug!("dftx : {:?}", dftx);

            match &dftx {
                DfTx::CreateMasternode(data) => data.index(services, &ctx)?,
                DfTx::UpdateMasternode(data) => data.index(services, &ctx)?,
                DfTx::ResignMasternode(data) => data.index(services, &ctx)?,
                // DfTx::AppointOracle(data) => data.index(services,&ctx)?,
                // DfTx::RemoveOracle(data) => data.index(services,&ctx)?,
                // DfTx::UpdateOracle(data) => data.index(services,&ctx)?,
                // DfTx::SetOracleData(data) => data.index(services,&ctx)?,
                DfTx::PoolSwap(data) => data.index(services, &ctx)?,
                DfTx::CompositeSwap(data) => data.index(services, &ctx)?,
                DfTx::PlaceAuctionBid(data) => data.index(services, &ctx)?,

                _ => (),
            }
            log_elapsed(start, &format!("Indexed tx {:?}", dftx));
        }

        index_transaction(services, ctx)?;
    }

    log_elapsed(start, "Indexed block");

    Ok(())
}

pub fn invalidate_block(block: Block<Transaction>) -> Result<()> {
    Ok(())
}
