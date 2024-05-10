#[cfg(test)]
mod tests {
    use std::{str::FromStr, sync::Arc};

    use ain_dftx::{common::CompactVec, price::CurrencyPair, types::oracles::AppointOracle};
    use bitcoin::{BlockHash, ScriptBuf, Txid};
    use defichain_rpc::json::blockchain::Transaction;
    use tempfile::tempdir;

    use crate::{
        indexer::{Context, Index}, model::{BlockContext, Oracle, OraclePriceFeed, PriceFeedsItem}, repository::RepositoryOps, storage::{ocean_store, SortOrder}, Services
    };



      #[test]
    fn test_save_and_retrieve_oracle() {
        // Setup the temporary storage for testing
        let temp_dir = tempdir().expect("Failed to create temporary directory");
        let path = temp_dir.path();
        let ocean_store_result = ocean_store::OceanStore::new(path);
        let ocean_store = match ocean_store_result {
            Ok(store) => Arc::new(store),
            Err(error) => panic!("Failed to create OceanStore: {}", error),
        };

        let services = Arc::new(Services::new(ocean_store));

        // Create an example Oracle instance
        let oracle = Oracle {
            id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
            owner_address: "a9149cd82492b8fadde74486ecebdb497934492b061187".to_string(),
            weightage: 1,
            price_feeds: vec![
                PriceFeedsItem { token: "TA".to_string(), currency: "USD".to_string() },
                PriceFeedsItem { token: "TB".to_string(), currency: "USD".to_string() },
                PriceFeedsItem { token: "TC".to_string(), currency: "USD".to_string() },
                PriceFeedsItem { token: "TD".to_string(), currency: "USD".to_string() },
            ],
            block: BlockContext {
                hash: BlockHash::from_str("84ac36b14a377fa47ed062e06fe949c448b34e543a98d6a00b8738e9d1cd27f1").unwrap(),
                height: 121,
                time: 1714980074,
                median_time: 1714980074,
            }
        };

        // Save the oracle to the ocean db
        services.oracle.by_id.put(&oracle.id, &oracle).expect("Failed to save oracle");
        // Retrieve the oracle from the system
        let retrieved_oracle = services.oracle.by_id.get(&oracle.id).expect("Failed to retrieve oracle").expect("Oracle not found");
        // Assert that the retrieved oracle matches the saved oracle
        assert_eq!(oracle.id, retrieved_oracle.id, "Retrieved oracle does not match saved oracle");
    }

    #[test]
    fn test_save_and_retrieve_oracle_price_feed() {
        // Setup the temporary storage for testing
        let temp_dir = tempdir().expect("Failed to create temporary directory");
        let path = temp_dir.path();
        let ocean_store_result = ocean_store::OceanStore::new(path);
        let ocean_store = match ocean_store_result {
            Ok(store) => Arc::new(store),
            Err(error) => panic!("Failed to create OceanStore: {}", error),
        };
        let services = Arc::new(Services::new(ocean_store));
        let feeds = vec![
            OraclePriceFeed {
                id: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap()),
                key: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323464366639356634323366303963663033663137626264633961653033666361366433613261656462633565303631356137613264323762663835616537633239".to_string(),
                token: "TA".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980187,
                amount: 110000000,
                block: BlockContext {
                    hash: BlockHash::from_str("175b254878fba4ec32b9de754330b4cc6c577d1999439ffa37e6348109c779fb").unwrap(),
                    height: 124,
                    time: 1714980187,
                    median_time: 1714980136,
                }
            },
            OraclePriceFeed {
                id: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap()),
                key: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323464366639356634323366303963663033663137626264633961653033666361366433613261656462633565303631356137613264323762663835616537633239".to_string(),
                token: "TB".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980187,
                amount: 210000000,
                block: BlockContext {
                    hash: BlockHash::from_str("175b254878fba4ec32b9de754330b4cc6c577d1999439ffa37e6348109c779fb").unwrap(),
                    height: 124,
                    time: 1714980187,
                    median_time: 1714980136,
                }
            },
            OraclePriceFeed {
                id: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap()),
                key: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323464366639356634323366303963663033663137626264633961653033666361366433613261656462633565303631356137613264323762663835616537633239".to_string(),
                token: "TC".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980187,
                amount: 310000000,
                block: BlockContext {
                    hash: BlockHash::from_str("175b254878fba4ec32b9de754330b4cc6c577d1999439ffa37e6348109c779fb").unwrap(),
                    height: 124,
                    time: 1714980187,
                    median_time: 1714980136,
                }
            },
            OraclePriceFeed {
                id: ("TD".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap()),
                key: ("TD".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323464366639356634323366303963663033663137626264633961653033666361366433613261656462633565303631356137613264323762663835616537633239".to_string(),
                token: "TD".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980187,
                amount: 410000000,
                block: BlockContext {
                    hash: BlockHash::from_str("175b254878fba4ec32b9de754330b4cc6c577d1999439ffa37e6348109c779fb").unwrap(),
                    height: 124,
                    time: 1714980187,
                    median_time: 1714980136,
                }
            },
		];

         // Save each OraclePriceFeed to the system
        for feed in &feeds {
            // services.oracle_price_feed.by_key.put(&feed.key.clone(), &feed.id).expect("Failed to save OraclePriceFeed Keys");
            services.oracle_price_feed.by_id.put(&feed.id, feed).expect("Failed to save OraclePriceFeed");
        }

        // //check key is fetched correctly
        // let key_1 = feeds[0].key.clone();
        // let retrieve_key_1 = services.oracle_price_feed.by_key.get(&key_1).expect("Failed to retrieve oracle_price_feed_key").expect("oracle_price_feed_key not found");
        // assert_eq!(feeds[0].id,retrieve_key_1,"Retrieved oracle does not match saved oracle price feed");
       
        // //check key list
        // let retrieve_key_list = services.oracle_price_feed.by_key.list(Some(feeds[1].key.clone()), SortOrder::Descending).expect("Failed to retrieve oracle_price_feed_key list");
        // let key_list: Vec<_> = retrieve_key_list.map(|res| res.expect("Error retrieving key")).collect();
        // print!("Using Key LIST {:?}",key_list);
        // assert!(!key_list.is_empty(), "The retrieved key list should not be empty");
        // assert_eq!(key_list[0].0, feeds[1].key, "The first key should match the starting key");


        //check id is fetched correctly
        let id_1 = feeds[0].id.clone();
        let retrieve_key_2 = services.oracle_price_feed.by_id.get(&id_1).expect("Failed to retrieve oracle_price_feed_key").expect("oracle_price_feed_key not found");
        assert_eq!(feeds[0].id,retrieve_key_2.id,"Retrieved oracle does not match saved oracle price feed");
       
        //check id list
        let retrieve_key_list_1 = services.oracle_price_feed.by_id.list(Some(feeds[1].id.clone()), SortOrder::Descending).expect("Failed to retrieve oracle_price_feed_key list");
        let id_list: Vec<_> = retrieve_key_list_1.map(|res| res.expect("Error retrieving key")).collect();
        print!("Using ID LIST {:?}",id_list);
        assert!(!id_list.is_empty(), "The retrieved ID list should not be empty");
        assert_eq!(id_list[0].0, feeds[1].id, "The first Id should match the starting key");
     
    }


}
