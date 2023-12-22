use anyhow::{anyhow, Result};
use bitcoin::string;
use rocksdb::IteratorMode;
use serde::{Deserialize, Serialize};

use crate::{
    database::db_manager::{ColumnFamilyOperations, MyIteratorMode, RocksDB, SortOrder},
    model::transaction::Transaction,
};

#[derive(Debug)]
pub struct TransactionVinDb {
    pub db: RocksDB,
}

impl TransactionVinDb {
    pub async fn get(&self, txid: String) -> Result<Option<Transaction>> {
        match self.db.get("transaction", txid.as_bytes()) {
            Ok(Some(value)) => {
                let trx: Transaction = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
                Ok(Some(trx))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn store(&self, txn: Transaction) -> Result<()> {
        let value = serde_json::to_string(&txn)?;
        let txid_key = txn.txid.clone();
        self.db
            .put("transaction", txid_key.as_bytes(), value.as_bytes())?;

        // Retrieve existing transaction IDs for the block hash, if any
        let mut txn_ids = match self
            .db
            .get("transaction_mapper", txn.block.hash.as_bytes())?
        {
            Some(bytes) => serde_json::from_slice::<Vec<String>>(&bytes)?,
            None => vec![],
        };

        // Add the new transaction ID to the list
        txn_ids.push(txn.txid);
        let txn_ids_value = serde_json::to_string(&txn_ids)?;
        self.db.put(
            "transaction_mapper",
            txn.block.hash.as_bytes(),
            txn_ids_value.as_bytes(),
        )?;
        Ok(())
    }
    pub async fn delete(&self, txid: String) -> Result<()> {
        match self.db.delete("transaction", txid.as_bytes()) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
    pub async fn query_by_block_hash(
        &self,
        hash: String,
        limit: i32,
        start_index: i32,
        sort_order: SortOrder,
    ) -> Result<Vec<Transaction>> {
        let iter_mode: IteratorMode = MyIteratorMode::from((sort_order, start_index)).into();
        let transaction: Result<Vec<_>> = self
            .db
            .iterator_by_id("transaction_mapper", &hash, iter_mode)?
            .take(limit as usize)
            .map(|result| {
                result
                    .map_err(|e| {
                        anyhow!("Error during iteration: {}", e).context("Contextual error message")
                    })
                    .and_then(|(_key, value)| {
                        let trx: Transaction = serde_json::from_slice(&value)?;
                        Ok(trx)
                    })
            })
            .collect();

        Ok(transaction?)
    }
}
