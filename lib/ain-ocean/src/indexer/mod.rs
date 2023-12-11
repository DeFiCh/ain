mod auction;
mod masternode;
mod oracle;
mod pool;

use dftx_rs::Transaction;

pub(crate) type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

pub(crate) trait Index {
    fn index(&self, context: &BlockContext, tx: Transaction) -> Result<()>;
    fn invalidate(&self);
}

use bitcoin::BlockHash;
use dftx_rs::{deserialize, Block, DfTx};
use log::debug;

pub(crate) struct BlockContext {
    height: u32,
    hash: BlockHash,
    time: u64,
    median_time: u64,
}

pub fn index_block(block: String, block_height: u32) -> Result<()> {
    debug!("[index_block] Indexing block...");

    let hex = hex::decode(block)?;
    let block = deserialize::<Block>(&hex)?;

    let context = BlockContext {
        height: block_height,
        hash: block.block_hash(),
        time: 0,        // TODO
        median_time: 0, // TODO
    };

    for tx in block.txdata {
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
                DfTx::CreateMasternode(data) => data.index(&context, tx)?,
                // DfTx::UpdateMasternode(data) => data.index(&context, tx)?,
                // DfTx::ResignMasternode(data) => data.index(&context, tx)?,
                // DfTx::AppointOracle(data) => data.index(&context, tx)?,
                // DfTx::RemoveOracle(data) => data.index(&context, tx)?,
                // DfTx::UpdateOracle(data) => data.index(&context, tx)?,
                // DfTx::SetOracleData(data) => data.index(&context, tx)?,
                // DfTx::PoolSwap(data) => data.index(&context, tx)?,
                // DfTx::CompositeSwap(data) => data.index(&context, tx)?,
                // DfTx::PlaceAuctionBid(data) => data.index(&context, tx)?,
                _ => (),
            }
        }
    }
    Ok(())
}

pub fn invalidate_block(block: String, block_height: u32) -> Result<()> {
    Ok(())
}
