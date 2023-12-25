use dftx_rs::{masternode::*, Transaction};
use log::debug;

use super::BlockContext;
use crate::{
    indexer::{Index, Result},
    model::{Masternode, MasternodeBlock},
    repository::RepositoryOps,
    SERVICES,
};

impl Index for CreateMasternode {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        debug!("[CreateMasternode] Indexing...");
        let txid = tx.txid();
        debug!("[CreateMasternode] Indexing {txid:?}");

        let masternode = Masternode {
            id: txid.to_string(),
            sort: format!("{}-{}", context.height, idx),
            owner_address: tx.output[1].script_pubkey.to_hex_string(),
            operator_address: tx.output[1].script_pubkey.to_hex_string(),
            creation_height: context.height,
            resign_height: -1,
            resign_tx: None,
            minted_blocks: 0,
            timelock: self.timelock.0.unwrap_or_default(),
            block: MasternodeBlock {
                hash: context.hash.to_string(),
                height: context.height,
                time: context.time,
                median_time: context.median_time,
            },
            collateral: tx.output[1].value.to_string(),
            history: None,
        };

        SERVICES.masternode.by_id.put(&txid, &masternode)?;
        SERVICES
            .masternode
            .by_height
            .put(&(context.height, idx), &txid.to_string())
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        debug!("[CreateMasternode] Invalidating...");
        let txid = tx.txid();
        SERVICES.masternode.by_id.delete(&txid)?;
        SERVICES.masternode.by_height.delete(&(context.height, idx))
    }
}

impl Index for UpdateMasternode {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        debug!("[UpdateMasternode] Indexing...");
        // TODO
        // Get mn
        // Update fields
        Ok(())
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        // TODO
        // Get mn
        // Restore from history
        Ok(())
    }
}

impl Index for ResignMasternode {
    fn index(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        debug!("[ResignMasternode] Indexing...");
        // TODO
        // Get mn
        // Set resign tx and resign height
        Ok(())
    }

    fn invalidate(&self, context: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        // TODO
        // Get mn
        // Set resign height to -1
        Ok(())
    }
}
