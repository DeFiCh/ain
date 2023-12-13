use anyhow::{anyhow, Result};

use crate::{
    database::db_manager::{ColumnFamilyOperations, RocksDB},
    model::raw_block::RawBlock,
};

#[derive(Debug)]
pub struct RawBlockDb {
    pub db: RocksDB,
}

impl RawBlockDb {
    pub async fn get(&self, hash: String) -> Result<Option<RawBlock>> {
        match self.db.get("raw_block", hash.as_bytes()) {
            Ok(Some(value)) => {
                let trx: RawBlock = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(trx))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }

    pub async fn store(&self, block: RawBlock) -> Result<()> {
        match serde_json::to_string(&block) {
            Ok(value) => {
                let key = block.id.clone();
                self.db.put("raw_block", key.as_bytes(), value.as_bytes())?;
                Ok(())
            }
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn delete(&self, hash: String) -> Result<()> {
        match self.db.delete("raw_block", hash.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}
