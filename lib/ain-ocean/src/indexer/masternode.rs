use bitcoin::{hashes::Hash, PubkeyHash, ScriptBuf, WPubkeyHash};
use dftx_rs::{masternode::*, Transaction};
use log::debug;

use super::BlockContext;
use crate::{
    indexer::{Index, Result},
    model::{HistoryItem, Masternode, MasternodeBlock},
    repository::RepositoryOps,
    SERVICES,
};

fn get_operator_script(hash: &PubkeyHash, r#type: u8) -> Result<ScriptBuf> {
    match r#type {
        0x1 => Ok(ScriptBuf::new_p2pkh(hash)),
        0x4 => Ok(ScriptBuf::new_p2wpkh(&WPubkeyHash::hash(
            hash.as_byte_array(),
        ))),
        _ => Err("Unsupported type".into()),
    }
}

impl Index for CreateMasternode {
    fn index(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        debug!("[CreateMasternode] Indexing...");
        let txid = tx.txid();

        let masternode = Masternode {
            id: txid,
            sort: format!("{}-{}", ctx.height, idx),
            owner_address: tx.output[1].script_pubkey.clone(),
            operator_address: get_operator_script(&self.operator_pub_key_hash, self.operator_type)?,
            creation_height: ctx.height,
            resign_height: None,
            resign_tx: None,
            minted_blocks: 0,
            timelock: self.timelock.0.unwrap_or_default(),
            block: MasternodeBlock {
                hash: ctx.hash,
                height: ctx.height,
                time: ctx.time,
                median_time: ctx.median_time,
            },
            collateral: tx.output[1].value.to_string(),
            history: Vec::new(),
        };

        SERVICES.masternode.by_id.put(&txid, &masternode)?;
        SERVICES.masternode.by_height.put(&(ctx.height, idx), &txid)
    }

    fn invalidate(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        debug!("[CreateMasternode] Invalidating...");
        SERVICES.masternode.by_id.delete(&tx.txid())?;
        SERVICES.masternode.by_height.delete(&(ctx.height, idx))
    }
}

impl Index for UpdateMasternode {
    fn index(&self, _ctx: &BlockContext, tx: Transaction, _idx: usize) -> Result<()> {
        debug!("[UpdateMasternode] Indexing...");
        if let Some(mut mn) = SERVICES.masternode.by_id.get(&self.node_id)? {
            mn.history.push(HistoryItem {
                owner_address: mn.owner_address.clone(),
                operator_address: mn.operator_address.clone(),
            });

            for update in self.updates.as_ref() {
                debug!("update : {:?}", update);
                match update.r#type {
                    0x1 => mn.owner_address = tx.output[1].script_pubkey.clone(),
                    0x2 => {
                        if let Some(hash) = update.address.address_pub_key_hash {
                            mn.operator_address = get_operator_script(&hash, update.address.r#type)?
                        }
                    }
                    _ => (),
                }
            }

            SERVICES.masternode.by_id.put(&self.node_id, &mn)?;
        }
        Ok(())
    }

    fn invalidate(&self, _ctx: &BlockContext, _tx: Transaction, _idx: usize) -> Result<()> {
        debug!("[UpdateMasternode] Invalidating...");
        if let Some(mut mn) = SERVICES.masternode.by_id.get(&self.node_id)? {
            if let Some(history_item) = mn.history.pop() {
                mn.owner_address = history_item.owner_address;
                mn.operator_address = history_item.operator_address;
            }

            SERVICES.masternode.by_id.put(&self.node_id, &mn)?;
        }
        Ok(())
    }
}

impl Index for ResignMasternode {
    fn index(&self, ctx: &BlockContext, tx: Transaction, _idx: usize) -> Result<()> {
        debug!("[ResignMasternode] Indexing...");
        if let Some(mn) = SERVICES.masternode.by_id.get(&self.node_id)? {
            SERVICES.masternode.by_id.put(
                &self.node_id,
                &Masternode {
                    resign_height: Some(ctx.height),
                    resign_tx: Some(tx.txid()),
                    ..mn
                },
            )?;
        }
        Ok(())
    }

    fn invalidate(&self, _ctx: &BlockContext, _tx: Transaction, _idx: usize) -> Result<()> {
        debug!("[ResignMasternode] Invalidating...");
        if let Some(mn) = SERVICES.masternode.by_id.get(&self.node_id)? {
            SERVICES.masternode.by_id.put(
                &self.node_id,
                &Masternode {
                    resign_height: None,
                    ..mn
                },
            )?;
        }
        Ok(())
    }
}
