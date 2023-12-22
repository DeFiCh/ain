use anyhow::{anyhow, Result};
use rocksdb::IteratorMode;

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::transaction_vout::TransactionVout,
};

#[derive(Debug)]
pub struct TransactionVoutDb {
    pub db: RocksDB,
}

impl TransactionVoutDb {
    pub async fn get(&self, tx_id: String, n: i64) -> Result<Option<TransactionVout>> {
        match self.db.get("transaction_vout", tx_id.as_bytes()) {
            Ok(Some(value)) => {
                let master_node: TransactionVout =
                    serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(master_node))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn store(&self, trx_out: TransactionVout) -> Result<()> {
        let value = serde_json::to_string(&trx_out)?;
        let key = trx_out.id.clone();
        self.db
            .put("transaction_vout", key.as_bytes(), value.as_bytes())?;

        // Accumulate transaction vout IDs for each txid
        let mut vout_ids = match self
            .db
            .get("transaction_vout_mapper", trx_out.txid.as_bytes())?
        {
            Some(bytes) => serde_json::from_slice::<Vec<String>>(&bytes)?,
            None => vec![],
        };
        vout_ids.push(trx_out.id);
        let vout_ids_value = serde_json::to_string(&vout_ids)?;
        self.db.put(
            "transaction_vout_mapper",
            trx_out.txid.as_bytes(),
            vout_ids_value.as_bytes(),
        )?;

        Ok(())
    }
    pub async fn delete(&self, trx_id: String) -> Result<()> {
        match self.db.delete("transaction_vout", trx_id.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn query(
        &self,
        tx_id: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<TransactionVout>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let transaction_vin: Result<Vec<_>> = self
            .db
            .iterator_by_id("transaction_vout", &tx_id, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let trx_vout: TransactionVout = serde_json::from_slice(&value)?;
                        Ok(trx_vout)
                    })
            })
            .collect();
        Ok(transaction_vin?)
    }
}
