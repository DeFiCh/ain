// use anyhow::{anyhow, Result};
// use rocksdb::IteratorMode;

// use crate::{
//     database::db_manager::{ColumnFamilyOperations, RocksDB, SortOrder},
//     model::transaction_vin::TransactionVin,
// };

// #[derive(Debug)]
// pub struct TransactionVinDb {
//     pub db: RocksDB,
// }

// impl TransactionVinDb {
//     pub async fn store(&self, trx_vin: TransactionVin) -> Result<()> {
//         match serde_json::to_string(&trx_vin) {
//             Ok(value) => {
//                 let key = trx_vin.id.clone();
//                 self.db
//                     .put("transaction_vin", key.as_bytes(), value.as_bytes())?;
//                 self.db.put(
//                     "transaction_vin_mapper",
//                     trx_vin.txid.as_bytes(),
//                     trx_vin.id.as_bytes(),
//                 )?;
//                 Ok(())
//             }
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn delete(&self, id: String) -> Result<()> {
//         match self.db.delete("transaction_vin", id.as_bytes()) {
//             Ok(_) => Ok(()),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn query(
//         &self,
//         tx_id: String,
//         limit: i32,
//         lt: i32,
//         sort_order: SortOrder,
//     ) -> Result<Vec<TransactionVin>> {
//         let iterator = self.db.iterator("transaction_vin", IteratorMode::End)?;
//         let mut trx_vin: Vec<TransactionVin> = Vec::new();
//         let collected_blocks: Vec<_> = iterator.collect();

//         for result in collected_blocks.into_iter().rev() {
//             let (key, value) = match result {
//                 Ok((key, value)) => (key, value),
//                 Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
//             };

//             let vin: TransactionVin = serde_json::from_slice(&value)?;
//             if vin.txid == tx_id {
//                 trx_vin.push(vin);
//                 if trx_vin.len() as i32 >= limit {
//                     break;
//                 }
//             }
//         }

//         // Sort blocks based on the specified sort order
//         match sort_order {
//             SortOrder::Ascending => trx_vin.sort_by(|a, b| a.txid.cmp(&b.txid)),
//             SortOrder::Descending => trx_vin.sort_by(|a, b| b.txid.cmp(&a.txid)),
//         }

//         Ok(trx_vin)
//     }
// }
