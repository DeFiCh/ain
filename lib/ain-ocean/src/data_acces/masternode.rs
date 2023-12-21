// use crate::{
//     database::db_manager::{ColumnFamilyOperations, RocksDB, SortOrder},
//     model::masternode::Masternode,
// };
// use anyhow::{anyhow, Result};
// use rocksdb::IteratorMode;
// use serde_json;

// pub struct MasterNodeDB {
//     pub db: RocksDB,
// }

// impl MasterNodeDB {
//     pub async fn query(
//         &self,
//         limit: i32,
//         lt: u32,
//         sort_order: SortOrder,
//     ) -> Result<Vec<Masternode>> {
//         let iter_mode: IteratorMode = sort_order.into();
//         let master_node: Result<Vec<_>> = self
//             .db
//             .iterator("masternode", iter_mode)?
//             .into_iter()
//             .take(limit as usize)
//             .map(|result| {
//                 result
//                     .map_err(|e| {
//                         anyhow!("Error during iteration: {}", e)
//                             .context("error master_node query error")
//                     })
//                     .and_then(|(_key, value)| {
//                         let stats: Masternode = serde_json::from_slice(&value)?;
//                         if stats.block.height < lt {
//                             Ok(stats)
//                         } else {
//                             Err(anyhow!("Value is not less than lt")
//                                 .context("Contextual error message"))
//                         }
//                     })
//             })
//             .collect();

//         master_node.and_then(|result| Ok(result))
//     }
//     pub async fn get(&self, id: String) -> Result<Option<Masternode>> {
//         match self.db.get("masternode", id.as_bytes()) {
//             Ok(Some(value)) => {
//                 let master_node: Masternode =
//                     serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
//                 Ok(Some(master_node))
//             }
//             Ok(None) => Ok(None),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn store(&self, stats: Masternode) -> Result<()> {
//         match serde_json::to_string(&stats) {
//             Ok(value) => {
//                 let key = stats.id.clone();
//                 self.db
//                     .put("masternode", key.as_bytes(), value.as_bytes())?;
//                 Ok(())
//             }
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn delete(&self, id: String) -> Result<()> {
//         match self.db.delete("masternode", id.as_bytes()) {
//             Ok(_) => Ok(()),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
// }
