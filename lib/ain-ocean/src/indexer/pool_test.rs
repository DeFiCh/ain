#[cfg(test)]
mod tests {
    use crate::{
        indexer::{pool::{PoolSwapAggregatedInterval, AGGREGATED_INTERVALS}, PoolSwapAggregated},
        model::{BlockContext, PoolSwapAggregatedAggregated},
        repository::{RepositoryOps, SecondaryIndex},
        storage::{ocean_store, SortOrder},
        PoolCreationHeight, Error, Result, Services,
    };
    use bitcoin::BlockHash;
    use log::debug;
    use std::{str::FromStr, sync::Arc};
    use tempfile::tempdir;
    use rust_decimal_macros::dec;

    fn get_bucket(interval: i64) -> i64 {
        1714980074 - (1714980074 & interval)
    }

    #[test]
    fn test_bucket() -> Result<()> {
        let temp_dir = tempdir().expect("Failed to create temporary directory");
        let path = temp_dir.path();
        let ocean_store_result = ocean_store::OceanStore::new(path);
        let ocean_store = match ocean_store_result {
            Ok(store) => Arc::new(store),
            Err(error) => panic!("Failed to create OceanStore: {}", error),
        };

        let services = Arc::new(Services::new(ocean_store));

        let pools = [
            PoolCreationHeight { id: 1, creation_height: 10 }, // 5
            PoolCreationHeight { id: 2, creation_height: 20 }, // 10
            PoolCreationHeight { id: 3, creation_height: 30 }, // 15
            PoolCreationHeight { id: 1, creation_height: 40 }, // 20
            PoolCreationHeight { id: 1, creation_height: 50 }, // 25
            PoolCreationHeight { id: 3, creation_height: 60 }, // 30
        ];

        let one_day = PoolSwapAggregatedInterval::OneHour as u32;
        let repo = &services.pool_swap_aggregated;
        let mut bucket = 1700000000;
        let mut n = 1;

        for interval in AGGREGATED_INTERVALS {
            for pool in pools.clone() {
                // bucket = get_bucket(PoolSwapAggregatedInterval::OneDay as i64);
                bucket += 5;
                n += 1;
                let padded = &(format!("{:064}", n));
                // let padded = &(format!("{:064}", bucket));
                let hash = BlockHash::from_str(padded).unwrap();
                // let hash = BlockHash::from_str(
                //     "84ac36b14a377fa47ed062e06fe949c448b34e543a98d6a00b8738e9d1cd27f1",
                // )
                // .unwrap();

                let aggregated = PoolSwapAggregated {
                    id: format!("{}-{interval}-{hash}", pool.id),
                    key: format!("{}-{interval}", pool.id),
                    bucket,
                    // aggregated: PoolSwapAggregatedAggregated {
                    //     amounts: Default::default(),
                    // },
                    block: BlockContext {
                        hash,
                        height: 100 + 1,
                        time: 9999999999,
                        median_time: 9999999999,
                    },
                };

                // println!("bucket: {:?}", bucket);
                let pool_swap_aggregated_key = (pool.id, interval, bucket);
                let pool_swap_aggregated_id = (pool.id, interval, hash);

                repo
                    .by_key
                    .put(&pool_swap_aggregated_key, &pool_swap_aggregated_id)?;

                repo.by_id.put(&pool_swap_aggregated_id, &aggregated)?;
            }
        }

        // let res = repo
        //     .by_key
        //     .list(Some((10, one_day, i64::MAX)), SortOrder::Descending)?
        //     .collect::<Vec<_>>();
        // println!("len: {:?}", res.len());
        // println!("res: {:?}", res);

        let res = repo
            .by_key
            // .list(Some((1, one_day, 0)), SortOrder::Ascending)?
            .list(Some((1, one_day, i64::MAX)), SortOrder::Descending)?
            // .take(1)
            .take_while(|item| {
                println!("item: {:?}", item);
                match item {
                    Ok((k, _)) => k.0 == 1 && k.1 == one_day,
                    _ => true,
                }
            })
            .map(|e| repo.by_key.retrieve_primary_value(e))
            // .map(|item| {
            //     let (_, id) = item?;
            //     println!("id: {:?}", id);
            //     let aggregated = repo.by_id.get(&id)?.ok_or("missing")?;
            //     Ok::<PoolSwapAggregated, Error>(aggregated)
            // })
            .collect::<Result<Vec<_>>>()?;
        println!("len(): {:?}", res.len());
        println!("res: {:?}", res);

        Ok(())
    }
}
