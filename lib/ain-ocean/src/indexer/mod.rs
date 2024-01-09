mod auction;
mod masternode;
mod oracle;
mod pool;

use dftx_rs::Transaction;

pub(crate) trait Index {
    fn index(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()>;
    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()>;
}

use bitcoin::BlockHash;
use dftx_rs::{deserialize, Block, DfTx};
use log::debug;

use crate::Result;

pub(crate) struct BlockContext {
    height: u32,
    hash: BlockHash,
    time: u64,
    median_time: u64,
}

pub fn index_block(block: String, block_height: u32) -> Result<()> {
    debug!("[index_block] Indexing block...");

    let hex = hex::decode(block)?;
    debug!("got hex");
    let block = deserialize::<Block>(&hex)?;
    debug!("got block");
    let ctx = BlockContext {
        height: block_height,
        hash: block.block_hash(),
        time: 0,        // TODO
        median_time: 0, // TODO
    };

    for (idx, tx) in block.txdata.into_iter().enumerate() {
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
            match dftx {
                DfTx::CreateMasternode(data) => data.index(&ctx, tx, idx)?,
                DfTx::UpdateMasternode(data) => data.index(&ctx, tx, idx)?,
                DfTx::ResignMasternode(data) => data.index(&ctx, tx, idx)?,
                // DfTx::AppointOracle(data) => data.index(&ctx, tx, idx)?,
                // DfTx::RemoveOracle(data) => data.index(&ctx, tx, idx)?,
                // DfTx::UpdateOracle(data) => data.index(&ctx, tx, idx)?,
                // DfTx::SetOracleData(data) => data.index(&ctx, tx, idx)?,
                // DfTx::PoolSwap(data) => data.index(&ctx, tx, idx)?,
                // DfTx::CompositeSwap(data) => data.index(&ctx, tx, idx)?,
                DfTx::PlaceAuctionBid(data) => data.index(&ctx, tx, idx)?,
                _ => (),
            }
        }
    }

    Ok(())
}

pub fn invalidate_block(block: String, block_height: u32) -> Result<()> {
    Ok(())
}
