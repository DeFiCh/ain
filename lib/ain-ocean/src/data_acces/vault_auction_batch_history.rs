use anyhow::{anyhow, Result};
use serde_json;

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::vault_auction_batch_history::VaultAuctionBatchHistory,
};

pub struct VaultAuctionDB {
    pub db: RocksDB,
}

impl VaultAuctionDB {
    pub async fn store(&self, auction: VaultAuctionBatchHistory) -> Result<()> {
        match serde_json::to_string(&auction) {
            Ok(value) => {
                let key = auction.id.clone();
                self.db
                    .put("vault_auction_history", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn get(&self, id: String) -> Result<Option<VaultAuctionBatchHistory>> {
        match self.db.get("vault_auction_history", id.as_bytes()) {
            Ok(Some(value)) => {
                let oracle: VaultAuctionBatchHistory =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(oracle))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, id: String) -> Result<()> {
        match self.db.delete("vault_auction_history", id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
