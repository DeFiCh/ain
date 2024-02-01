use std::sync::Arc;

use bitcoin::{hashes::Hash, PubkeyHash, ScriptBuf, WPubkeyHash};
use defichain_rpc::json::blockchain::VinStandard;
use dftx_rs::masternode::*;
use log::debug;

use super::Context;
use crate::{
    indexer::{Index, Result},
    model::{HistoryItem, Masternode},
    repository::RepositoryOps,
    Services,
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
    fn index(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[CreateMasternode] Indexing...");
        let txid = ctx.tx.txid;
        let Some(ref addresses) = ctx.tx.vout[1].script_pub_key.addresses else {
            return Err("Missing owner address".into());
        };

        let masternode = Masternode {
            id: txid,
            sort: format!("{}-{}", ctx.block.height, ctx.tx_idx),
            owner_address: addresses[0].clone(),
            operator_address: get_operator_script(&self.operator_pub_key_hash, self.operator_type)?
                .to_hex_string(),
            creation_height: ctx.block.height,
            resign_height: None,
            resign_tx: None,
            minted_blocks: 0,
            timelock: self.timelock.0.unwrap_or_default(),
            block: ctx.block.clone(),
            collateral: ctx.tx.vout[1].value,
            history: Vec::new(),
        };

        services.masternode.by_id.put(&txid, &masternode)?;
        services
            .masternode
            .by_height
            .put(&(ctx.block.height, ctx.tx_idx), &txid)
    }

    fn invalidate(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[CreateMasternode] Invalidating...");
        services.masternode.by_id.delete(&ctx.tx.txid)?;
        services
            .masternode
            .by_height
            .delete(&(ctx.block.height, ctx.tx_idx))
    }
}

impl Index for UpdateMasternode {
    fn index(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[UpdateMasternode] Indexing...");
        if let Some(mut mn) = services.masternode.by_id.get(&self.node_id)? {
            mn.history.push(HistoryItem {
                owner_address: mn.owner_address.clone(),
                operator_address: mn.operator_address.clone(),
            });

            for update in self.updates.as_ref() {
                debug!("update : {:?}", update);
                match update.r#type {
                    0x1 => {
                        if let Some(ref addresses) = ctx.tx.vout[1].script_pub_key.addresses {
                            mn.owner_address = addresses[0].clone()
                        }
                    }
                    0x2 => {
                        if let Some(hash) = update.address.address_pub_key_hash {
                            mn.operator_address =
                                get_operator_script(&hash, update.address.r#type)?.to_hex_string()
                        }
                    }
                    _ => (),
                }
            }

            services.masternode.by_id.put(&self.node_id, &mn)?;
        }
        Ok(())
    }

    fn invalidate(&self, services: Arc<Services>, _ctx: &Context) -> Result<()> {
        debug!("[UpdateMasternode] Invalidating...");
        if let Some(mut mn) = services.masternode.by_id.get(&self.node_id)? {
            if let Some(history_item) = mn.history.pop() {
                mn.owner_address = history_item.owner_address;
                mn.operator_address = history_item.operator_address;
            }

            services.masternode.by_id.put(&self.node_id, &mn)?;
        }
        Ok(())
    }
}

impl Index for ResignMasternode {
    fn index(&self, services: Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[ResignMasternode] Indexing...");
        if let Some(mn) = services.masternode.by_id.get(&self.node_id)? {
            services.masternode.by_id.put(
                &self.node_id,
                &Masternode {
                    resign_height: Some(ctx.block.height),
                    resign_tx: Some(ctx.tx.txid),
                    ..mn
                },
            )?;
        }
        Ok(())
    }

    fn invalidate(&self, services: Arc<Services>, _ctx: &Context) -> Result<()> {
        debug!("[ResignMasternode] Invalidating...");
        if let Some(mn) = services.masternode.by_id.get(&self.node_id)? {
            services.masternode.by_id.put(
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
