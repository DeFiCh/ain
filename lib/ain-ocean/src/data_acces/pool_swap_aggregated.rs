// use anyhow::{anyhow, Result};
// use rocksdb::IteratorMode;

// use crate::{
//     database::db_manager::{ColumnFamilyOperations, RocksDB, SortOrder},
//     model::poolswap_aggregated::PoolSwapAggregated,
// };

// pub struct PoolSwapAggregatedDb {
//     pub db: RocksDB,
// }

// impl PoolSwapAggregatedDb {
//     pub async fn query(
//         &self,
//         id: String,
//         limit: i32,
//         lt: String,
//         sort_order: SortOrder,
//     ) -> Result<(Vec<PoolSwapAggregated>)> {
//         let iterator = self
//             .db
//             .iterator("pool_swap_aggregated", IteratorMode::End)?;
//         let mut pool_swap: Vec<PoolSwapAggregated> = Vec::new();
//         let collected_blocks: Vec<_> = iterator.collect();

//         for result in collected_blocks.into_iter().rev() {
//             let (key, value) = match result {
//                 Ok((key, value)) => (key, value),
//                 Err(err) => return Err(anyhow!("Error during iteration: {}", err)),
//             };

//             let ps: PoolSwapAggregated = serde_json::from_slice(&value)?;
//             if ps.id == id {
//                 pool_swap.push(ps);
//                 if pool_swap.len() as i32 >= limit {
//                     break;
//                 }
//             }
//         }

//         // Sort blocks based on the specified sort order
//         match sort_order {
//             SortOrder::Ascending => pool_swap.sort_by(|a, b| a.id.cmp(&b.id)),
//             SortOrder::Descending => pool_swap.sort_by(|a, b| b.id.cmp(&a.id)),
//         }

//         Ok(pool_swap)
//     }
//     pub async fn put(&self, aggregated: PoolSwapAggregated) -> Result<()> {
//         match serde_json::to_string(&aggregated) {
//             Ok(value) => {
//                 let key = aggregated.id.clone();
//                 self.db
//                     .put("pool_swap_aggregated", key.as_bytes(), value.as_bytes())?;
//                 Ok(())
//             }
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn get(&self, id: String) -> Result<PoolSwapAggregated> {
//         match self.db.get("pool_swap_aggregated", id.as_bytes()) {
//             Ok(Some(value)) => {
//                 let pool_swap: PoolSwapAggregated =
//                     serde_json::from_slice(&value).map_err(|e| anyhow!(e))?;
//                 Ok(pool_swap)
//             }
//             Ok(None) => Err(anyhow!("No data found for the given ID")),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
//     pub async fn delete(&self, id: String) -> Result<()> {
//         match self.db.delete("pool_swap_aggregated", id.as_bytes()) {
//             Ok(_) => Ok(()),
//             Err(e) => Err(anyhow!(e)),
//         }
//     }
// }
