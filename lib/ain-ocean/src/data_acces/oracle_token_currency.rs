// use anyhow::{anyhow, Result};
// use rocksdb::IteratorMode;

// use crate::{
//     database::db_manager::{ColumnFamilyOperations, RocksDB, SortOrder},
//     model::oracle_token_currency::OracleTokenCurrency,
// };

// pub struct OracleTokenCurrencyDb {
//     pub db: RocksDB,
// }

// impl OracleTokenCurrencyDb {
//     pub async fn query(
//         &self,
//         oracle_id: String,
//         limit: i32,
//         lt: String,
//         sort_order: SortOrder,
//     ) -> Result<Vec<OracleTokenCurrency>> {
//         let iterator = self
//             .db
//             .iterator("oracle_token_currency", IteratorMode::End)?;
//         let mut oracle_tc: Vec<OracleTokenCurrency> = Vec::new();
//         let collected_blocks: Vec<_> = iterator.collect();

//         for result in collected_blocks.into_iter().rev() {
//             let (key, value) = match result {
//                 Ok((key, value)) => (key, value),
//                 Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
//             };

//             let oracle: OracleTokenCurrency = serde_json::from_slice(&value)?;
//             if oracle.key == oracle_id {
//                 oracle_tc.push(oracle);
//                 if oracle_tc.len() as i32 >= limit {
//                     break;
//                 }
//             }
//         }

//         // Sort blocks based on the specified sort order
//         match sort_order {
//             SortOrder::Ascending => oracle_tc.sort_by(|a: &OracleTokenCurrency, b| a.id.cmp(&b.id)),
//             SortOrder::Descending => oracle_tc.sort_by(|a, b| b.id.cmp(&a.id)),
//         }

//         Ok(oracle_tc)
//     }
//     pub async fn put(&self, oracle_token: OracleTokenCurrency) -> Result<()> {
//         match serde_json::to_string(&oracle_token) {
//             Ok(value) => {
//                 let key = oracle_token.id.clone();
//                 self.db
//                     .put("oracle_token_currency", key.as_bytes(), value.as_bytes())?;
//                 Ok(())
//             }
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn delete(&self, id: String) -> Result<()> {
//         match self.db.delete("oracle_token_currency", id.as_bytes()) {
//             Ok(_) => Ok(()),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
// }
