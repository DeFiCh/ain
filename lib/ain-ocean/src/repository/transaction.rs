// use anyhow::{anyhow, Result};
// use serde::{Deserialize, Serialize};

// use crate::{
//     database::db_manager::{ColumnFamilyOperations, RocksDB},
//     model::transaction::Transaction,
// };

// #[derive(Debug)]
// pub struct TransactionVinDb {
//     pub db: RocksDB,
// }

// impl TransactionVinDb {
//     pub async fn get(&self, txid: String) -> Result<Option<Transaction>> {
//         match self.db.get("transaction", txid.as_bytes()) {
//             Ok(Some(value)) => {
//                 let trx: Transaction = serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
//                 Ok(Some(trx))
//             }
//             Ok(None) => Ok(None),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn store(&self, txn: Transaction) -> Result<()> {
//         let value = serde_json::to_string(&txn)?;
//         let txid_key = txn.txid.clone();
//         self.db
//             .put("transaction", txid_key.as_bytes(), value.as_bytes())?;

//         // Retrieve existing transaction IDs for the block hash, if any
//         let mut txn_ids = match self
//             .db
//             .get("transaction_mapper", txn.block.hash.as_bytes())?
//         {
//             Some(bytes) => serde_json::from_slice::<Vec<String>>(&bytes)?,
//             None => vec![],
//         };

//         // Add the new transaction ID to the list
//         txn_ids.push(txn.txid);
//         let txn_ids_value = serde_json::to_string(&txn_ids)?;
//         self.db.put(
//             "transaction_mapper",
//             txn.block.hash.as_bytes(),
//             txn_ids_value.as_bytes(),
//         )?;
//         Ok(())
//     }
//     pub async fn delete(&self, txid: String) -> Result<()> {
//         match self.db.delete("transaction", txid.as_bytes()) {
//             Ok(_) => Ok(()),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn query_by_block_hash(
//         &self,
//         hash: String,
//         limit: i32,
//         lt: i32,
//     ) -> Result<Vec<Transaction>> {
//         let mut transactions = Vec::new();

//         // Retrieve the transaction ID(s) associated with the block hash
//         match self.db.get("transaction_mapper", hash.as_bytes()) {
//             Ok(Some(txn_id_bytes)) => {
//                 // Assuming one block hash maps to multiple transaction IDs
//                 println!("the value in trx{:?}", txn_id_bytes);
//                 let txn_ids: Vec<String> = serde_json::from_slice::<Vec<String>>(&txn_id_bytes)?;

//                 for txn_id in txn_ids.iter().take(limit as usize) {
//                     // Retrieve the transaction details for each transaction ID
//                     match self.db.get("transaction", txn_id.as_bytes()) {
//                         Ok(Some(txn_bytes)) => {
//                             let txn: Transaction = serde_json::from_slice(&txn_bytes)?;
//                             transactions.push(txn);
//                         }
//                         Ok(None) => {
//                             return Err(anyhow!("Transaction not found for ID: {}", txn_id))
//                         }
//                         Err(e) => return Err(anyhow!("Database error: {}", e)),
//                     }
//                 }
//             }
//             Ok(None) => return Err(anyhow!("No transactions found for block hash: {}", hash)),
//             Err(e) => return Err(anyhow!("Database error: {}", e)),
//         }

//         Ok(transactions)
//     }
// }
