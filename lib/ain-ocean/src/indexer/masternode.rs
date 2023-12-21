use dftx_rs::{masternode::*, Transaction};
use log::debug;

use super::BlockContext;
use crate::{
    indexer::{Index, Result},
    model::masternode::{Masternode, MasternodeBlock},
};

impl Index for CreateMasternode {
    fn index(&self, context: &BlockContext, tx: Transaction) -> Result<()> {
        debug!("[CreateMasternode] Indexing...");

        let masternode = Masternode {
            id: tx.txid().to_string(),
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
            sort: None,
            history: None,
        };

        Ok(())
    }

    fn invalidate(&self) {
        todo!()
    }
}

impl Index for UpdateMasternode {
    fn index(&self, context: &BlockContext, tx: Transaction) -> Result<()> {
        debug!("[UpdateMasternode] Indexing...");
        // TODO
        // Get mn
        // Update fields
        Ok(())
    }

    fn invalidate(&self) {
        // TODO
        // Get mn
        // Restore from history
    }
}

impl Index for ResignMasternode {
    fn index(&self, context: &BlockContext, tx: Transaction) -> Result<()> {
        debug!("[ResignMasternode] Indexing...");
        // TODO
        // Get mn
        // Set resign tx and resign height
        Ok(())
    }

    fn invalidate(&self) {
        // TODO
        // Get mn
        // Set resign height to -1
    }
}
