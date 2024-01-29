mod auction;
mod masternode;
mod oracle;
mod pool;
pub mod transaction;
pub mod tx_result;

use std::time::Instant;

use dftx_rs::{deserialize, Block, DfTx, Transaction};
use log::debug;

use crate::{
    model::{Block as BlockMapper, BlockContext},
    repository::RepositoryOps,
    Result, SERVICES,
};

pub(crate) trait Index {
    fn index(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()>;
    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()>;
}

pub struct BlockV2Info {
    pub height: u32,
    pub difficulty: u32,
    pub version: i32,
    pub median_time: i64,
    pub minter_block_count: u64,
    pub size: usize,
    pub size_stripped: usize,
    pub weight: i64,
    pub stake_modifier: String,
    pub minter: String,
    pub masternode: String,
}

fn log_elapsed(previous: Instant, msg: &str) {
    let now = Instant::now();
    println!("{} in {} ms", msg, now.duration_since(previous).as_millis());
}

pub fn index_block(encoded_block: String, info: &BlockV2Info) -> Result<()> {
    debug!("[index_block] Indexing block...");
    let start = Instant::now();

    let hex = hex::decode(&encoded_block)?;
    debug!("got hex");
    let block = deserialize::<Block>(&hex)?;
    debug!("got block");
    let block_hash = block.block_hash();
    let ctx = BlockContext {
        height: info.height,
        hash: block_hash,
        time: 0,        // TODO
        median_time: 0, // TODO
    };
    let block_mapper = BlockMapper {
        id: block_hash.to_string(),
        hash: block_hash.to_string(),
        previous_hash: block.header.prev_blockhash.to_string(),
        height: info.height,
        version: info.version,
        time: block.header.time,
        median_time: info.median_time,
        transaction_count: block.txdata.len(),
        difficulty: info.difficulty,
        masternode: info.masternode.to_owned(),
        minter: info.minter.to_owned(),
        minter_block_count: info.minter_block_count,
        stake_modifier: info.stake_modifier.to_owned(),
        merkleroot: block.header.merkle_root.to_string(),
        size: info.size,
        size_stripped: info.size_stripped,
        weight: info.weight,
    };

    SERVICES.block.raw.put(&ctx.hash, &encoded_block)?;
    SERVICES.block.by_id.put(&ctx.hash, &block_mapper)?;
    SERVICES.block.by_height.put(&ctx.height, &block_hash)?;

    for (idx, tx) in block.txdata.into_iter().enumerate() {
        let start = Instant::now();
        let bytes = tx.output[0].script_pubkey.as_bytes();
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
                DfTx::CreateMasternode(data) => data.index(&ctx, tx, idx)?,
                DfTx::UpdateMasternode(data) => data.index(&ctx, tx, idx)?,
                DfTx::ResignMasternode(data) => data.index(&ctx, tx, idx)?,
                // DfTx::AppointOracle(data) => data.index(&ctx, tx, idx)?,
                // DfTx::RemoveOracle(data) => data.index(&ctx, tx, idx)?,
                // DfTx::UpdateOracle(data) => data.index(&ctx, tx, idx)?,
                // DfTx::SetOracleData(data) => data.index(&ctx, tx, idx)?,
                DfTx::PoolSwap(data) => data.index(&ctx, tx, idx)?,
                DfTx::CompositeSwap(data) => data.index(&ctx, tx, idx)?,
                DfTx::PlaceAuctionBid(data) => data.index(&ctx, tx, idx)?,
                _ => (),
            }
            log_elapsed(start, &format!("Indexed tx {:?}", dftx));
        }
    }

    log_elapsed(start, "Indexed block");

    Ok(())
}

pub fn invalidate_block(block: String, info: &BlockV2Info) -> Result<()> {
    Ok(())
}
