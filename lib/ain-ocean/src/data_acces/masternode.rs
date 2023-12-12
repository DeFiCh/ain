use crate::database::db_manger::ColumnFamilyOperations;
use crate::database::db_manger::{RocksDB, SortOrder};
use crate::model::masternode::Masternode;
use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::masternode::Masternode,
};

pub struct MasterNodeDB {
    pub db: RocksDB,
}

impl MasterNodeDB {
    pub async fn query(
        &self,
        limit: i32,
        lt: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<Masternode>> {
        let iterator = self.db.iterator("masternode", IteratorMode::End)?;
        let mut master_node: Vec<Masternode> = Vec::new();
        let collected_blocks: Vec<_> = iterator.collect();

        for result in collected_blocks.into_iter().rev() {
            let (key, value) = match result {
                Ok((key, value)) => (key, value),
                Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
            };

            let master_stats: Masternode = serde_json::from_slice(&value)?;
            master_node.push(master_stats);

            if master_node.len() == limit as usize {
                break;
            }
        }

        // Sort blocks based on the specified sort order
        match sort_order {
            SortOrder::Ascending => master_node.sort_by(|a, b| a.block.height.cmp(&b.block.height)),
            SortOrder::Descending => {
                master_node.sort_by(|a, b| b.block.height.cmp(&a.block.height))
            }
        }

        Ok(master_node)
    }
    pub async fn get(&self, id: String) -> Result<Option<Masternode>> {
        match self.db.get("masternode", id.as_bytes()) {
            Ok(Some(value)) => {
                let master_node: Masternode =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(master_node))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn store(&self, stats: Masternode) -> Result<()> {
        match serde_json::to_string(&stats) {
            Ok(value) => {
                let key = stats.id.clone();
                self.db
                    .put("masternode", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("masternode", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
