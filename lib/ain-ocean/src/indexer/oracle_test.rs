#[cfg(test)]
mod tests {
    use std::{ptr::eq, str::FromStr, sync::Arc};

    use ain_dftx::{
        common::CompactVec,
        oracles::SetOracleData,
        price::{CurrencyPair, TokenAmount, TokenPrice},
        types::oracles::AppointOracle,
    };
    use bitcoin::{BlockHash, ScriptBuf, Txid};
    use defichain_rpc::json::blockchain::Transaction;
    use tempfile::tempdir;

    use crate::{
        indexer::{Context, Index},
        model::{BlockContext, Oracle, OraclePriceFeed, OracleTokenCurrency, PriceFeedsItem},
        repository::RepositoryOps,
        storage::{ocean_store, SortOrder},
        Services,
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
            id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5")
                .unwrap(),
            owner_address: "a9149cd82492b8fadde74486ecebdb497934492b061187".to_string(),
            weightage: 1,
            price_feeds: vec![
                PriceFeedsItem {
                    token: "TA".to_string(),
                    currency: "USD".to_string(),
                },
                PriceFeedsItem {
                    token: "TB".to_string(),
                    currency: "USD".to_string(),
                },
                PriceFeedsItem {
                    token: "TC".to_string(),
                    currency: "USD".to_string(),
                },
                PriceFeedsItem {
                    token: "TD".to_string(),
                    currency: "USD".to_string(),
                },
            ],
            block: BlockContext {
                hash: BlockHash::from_str(
                    "84ac36b14a377fa47ed062e06fe949c448b34e543a98d6a00b8738e9d1cd27f1",
                )
                .unwrap(),
                height: 121,
                time: 1714980074,
                median_time: 1714980074,
            },
        };

        // Save the oracle to the ocean db
        services
            .oracle
            .by_id
            .put(&oracle.id, &oracle)
            .expect("Failed to save oracle");
        // Retrieve the oracle from the system
        let retrieved_oracle = services
            .oracle
            .by_id
            .get(&oracle.id)
            .expect("Failed to retrieve oracle")
            .expect("Oracle not found");
        // Assert that the retrieved oracle matches the saved oracle
        assert_eq!(
            oracle.id, retrieved_oracle.id,
            "Retrieved oracle does not match saved oracle"
        );
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
            services
                .oracle_price_feed
                .by_id
                .put(&feed.id, feed)
                .expect("Failed to save OraclePriceFeed");
        }
        let retrieve_key_list = services
            .oracle_price_feed
            .by_id
            .list(None, SortOrder::Descending)
            .expect("Failed to retrieve oracle_price_feed_key list")
            .map(|res| res.expect("Error retrieving key"))
            .collect::<Vec<_>>();

        // Check if the retrieved feed ID matches the expected format
        for (_, feed) in &retrieve_key_list {
            let (token, currency, oracle_id, _) = &feed.id;
            if token.eq(&feeds[0].token)
                && currency.eq(&feeds[0].currency)
                && oracle_id.eq(&&feeds[0].oracle_id)
            {
                println!("Found matching feed: {:?}", feed);
            }
        }
        assert!(
            !retrieve_key_list.is_empty(),
            "The filtered list should not be empty"
        );
    }

    #[test]
    fn test_save_and_retrieve_multiple_oracle_price_feed() {
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

        let feeds_1 = vec![
            OraclePriceFeed {
                id: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TA".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 100000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
            OraclePriceFeed {
                id: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TB".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 200000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
            OraclePriceFeed {
                id: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TC".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 300000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
		];

        let feeds_2 = vec![
            OraclePriceFeed {
                id: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("574b3c7ef4cc572618a639c568b6b4dee0f6c99da9057e7756e9b3b338feae7c").unwrap()),
                key: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323635373462336337656634636335373236313861363339633536386236623464656530663663393964613930353765373735366539623362333338666561653763".to_string(),
                token: "TA".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980270,
                amount: 100000000,
                block: BlockContext {
                    hash: BlockHash::from_str("134894c63b9469a18f2ab314b10012baf658a122cae877d62cfbbfae3ab64ec2").unwrap(),
                    height: 126,
                    time: 1714980270,
                    median_time: 1714980187,
                }
            },
            OraclePriceFeed {
                id: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("574b3c7ef4cc572618a639c568b6b4dee0f6c99da9057e7756e9b3b338feae7c").unwrap()),
                key: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323635373462336337656634636335373236313861363339633536386236623464656530663663393964613930353765373735366539623362333338666561653763".to_string(),
                token: "TB".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980270,
                amount: 200000000,
                block: BlockContext {
                    hash: BlockHash::from_str("134894c63b9469a18f2ab314b10012baf658a122cae877d62cfbbfae3ab64ec2").unwrap(),
                    height: 126,
                    time: 1714980270,
                    median_time: 1714980187,
                }
            },
		];

        for feed in &feeds {
            services
                .oracle_price_feed
                .by_id
                .put(&feed.id, feed)
                .expect("Failed to save OraclePriceFeed");
        }

        for feed_1 in &feeds_1 {
            services
                .oracle_price_feed
                .by_id
                .put(&feed_1.id, feed_1)
                .expect("Failed to save OraclePriceFeed");
        }

        for feed_2 in &feeds_2 {
            services
                .oracle_price_feed
                .by_id
                .put(&feed_2.id, feed_2)
                .expect("Failed to save OraclePriceFeed");
        }

        let price_feed_list = services
            .oracle_price_feed
            .by_id
            .list(None, SortOrder::Descending)
            .expect("Failed to retrieve oracle_price_feed_key list")
            .map(|res| res.expect("Error retrieving key"))
            .collect::<Vec<_>>();

        for (_, feed) in &price_feed_list {
            let (token, currency, oracle_id, _) = &feed.id;
            if token.eq(&feeds[1].token)
                && currency.eq(&feeds[1].currency)
                && oracle_id.eq(&&feeds[1].oracle_id)
            {
                println!("Found matching feed: {:?}", feed);
            }
        }
        assert!(
            !price_feed_list.is_empty(),
            "The filtered list should not be empty"
        );
    }

    #[test]
    fn test_get_oracle_price_feed_list() {
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

        let feeds_1 = vec![
            OraclePriceFeed {
                id: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TA".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 100000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
            OraclePriceFeed {
                id: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TB".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 200000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
            OraclePriceFeed {
                id: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TC".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 300000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
		];

        let feeds_2 = vec![
            OraclePriceFeed {
                id: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("574b3c7ef4cc572618a639c568b6b4dee0f6c99da9057e7756e9b3b338feae7c").unwrap()),
                key: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323635373462336337656634636335373236313861363339633536386236623464656530663663393964613930353765373735366539623362333338666561653763".to_string(),
                token: "TA".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980270,
                amount: 100000000,
                block: BlockContext {
                    hash: BlockHash::from_str("134894c63b9469a18f2ab314b10012baf658a122cae877d62cfbbfae3ab64ec2").unwrap(),
                    height: 126,
                    time: 1714980270,
                    median_time: 1714980187,
                }
            },
            OraclePriceFeed {
                id: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("574b3c7ef4cc572618a639c568b6b4dee0f6c99da9057e7756e9b3b338feae7c").unwrap()),
                key: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323635373462336337656634636335373236313861363339633536386236623464656530663663393964613930353765373735366539623362333338666561653763".to_string(),
                token: "TB".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980270,
                amount: 200000000,
                block: BlockContext {
                    hash: BlockHash::from_str("134894c63b9469a18f2ab314b10012baf658a122cae877d62cfbbfae3ab64ec2").unwrap(),
                    height: 126,
                    time: 1714980270,
                    median_time: 1714980187,
                }
            },
		];

        for feed in &feeds {
            services
                .oracle_price_feed
                .by_id
                .put(&feed.id, feed)
                .expect("Failed to save OraclePriceFeed");
        }

        for feed_1 in &feeds_1 {
            services
                .oracle_price_feed
                .by_id
                .put(&feed_1.id, feed_1)
                .expect("Failed to save OraclePriceFeed");
        }

        for feed_2 in &feeds_2 {
            services
                .oracle_price_feed
                .by_id
                .put(&feed_2.id, feed_2)
                .expect("Failed to save OraclePriceFeed");
        }

        let price_feed_list = services
            .oracle_price_feed
            .by_id
            .list(None, SortOrder::Descending)
            .expect("Failed to retrieve oracle_price_feed_key list")
            .map(|res| res.expect("Error retrieving key"))
            .collect::<Vec<_>>();

        for (_, feed) in &price_feed_list {
            let (token, currency, oracle_id, _) = &feed.id;
            if token.eq(&feeds[1].token)
                && currency.eq(&feeds[1].currency)
                && oracle_id.eq(&&feeds[1].oracle_id)
            {
                println!("Found matching feed: {:?}", feed);
            }
        }
        assert!(
            !price_feed_list.is_empty(),
            "The filtered list should not be empty"
        );
    }

    #[test]
    fn test_index_set_oracle_oracle() {
        let temp_dir = tempdir().expect("Failed to create temporary directory");
        let path = temp_dir.path();
        let ocean_store_result = ocean_store::OceanStore::new(path);
        let ocean_store = match ocean_store_result {
            Ok(ocean_store) => Arc::new(ocean_store),
            Err(error) => {
                panic!("Failed to create OceanStore: {}", error);
            }
        };

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

        let feeds_1 = vec![
            OraclePriceFeed {
                id: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TA".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 100000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
            OraclePriceFeed {
                id: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TB".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 200000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
            OraclePriceFeed {
                id: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("94e0883205b425de5b5dd52a208f4cd1f3d7e09d066b0fd091b9bc7513b33a34").unwrap()),
                key: ("TC".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323539346530383833323035623432356465356235646435326132303866346364316633643765303964303636623066643039316239626337353133623333613334".to_string(),
                token: "TC".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980243,
                amount: 300000000,
                block: BlockContext {
                    hash: BlockHash::from_str("a93730388be038ac52d1f0ed869877a5b0189bb786a9809c0205a45d98b0d948").unwrap(),
                    height: 125,
                    time: 1714980243,
                    median_time: 1714980167,
                }
            },
		];

        let feeds_2 = vec![
            OraclePriceFeed {
                id: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("574b3c7ef4cc572618a639c568b6b4dee0f6c99da9057e7756e9b3b338feae7c").unwrap()),
                key: ("TA".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323635373462336337656634636335373236313861363339633536386236623464656530663663393964613930353765373735366539623362333338666561653763".to_string(),
                token: "TA".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980270,
                amount: 120000000,
                block: BlockContext {
                    hash: BlockHash::from_str("134894c63b9469a18f2ab314b10012baf658a122cae877d62cfbbfae3ab64ec2").unwrap(),
                    height: 126,
                    time: 1714980270,
                    median_time: 1714980187,
                }
            },
            OraclePriceFeed {
                id: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(), Txid::from_str("574b3c7ef4cc572618a639c568b6b4dee0f6c99da9057e7756e9b3b338feae7c").unwrap()),
                key: ("TB".to_string(), "USD".to_string(), Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap()),
                sort: "31323635373462336337656634636335373236313861363339633536386236623464656530663663393964613930353765373735366539623362333338666561653763".to_string(),
                token: "TB".to_string(),
                currency: "USD".to_string(),
                oracle_id: Txid::from_str("33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5").unwrap(),
                txid: Txid::from_str("d6f95f423f09cf03f17bbdc9ae03fca6d3a2aedbc5e0615a7a2d27bf85ae7c29").unwrap(),
                time: 1714980270,
                amount: 200000000,
                block: BlockContext {
                    hash: BlockHash::from_str("134894c63b9469a18f2ab314b10012baf658a122cae877d62cfbbfae3ab64ec2").unwrap(),
                    height: 126,
                    time: 1714980270,
                    median_time: 1714980187,
                }
            },
		];

        let services = Arc::new(Services::new(ocean_store));
        for feed in feeds {
            let oracle_token_currency = OracleTokenCurrency {
                id: feed.key,
                key: (feed.token.to_owned(), feed.currency.to_owned()),

                token: feed.token.to_owned(),
                currency: feed.currency.to_owned(),
                oracle_id: feed.oracle_id,
                weightage: 1,
                block: feed.block.clone(),
            };

            services
                .oracle_token_currency
                .by_id
                .put(&oracle_token_currency.id, &oracle_token_currency)
                .expect("Failed to save oracle_token_currency");

            let block_context = BlockContext {
                hash: feed.block.hash,
                height: feed.block.height,
                time: feed.block.time,
                median_time: feed.block.median_time,
            };

            let transaction = Transaction {
                txid: feed.txid,
                hash: feed.block.hash.to_string(),
                version: 1,
                size: 100,
                vsize: 90,
                weight: 80,
                locktime: 0,
                vin: vec![],
                vout: vec![],
                hex: "transaction_hex".to_string(),
            };

            let set_oracle = SetOracleData {
                oracle_id: feed.oracle_id,
                timestamp: feed.time as i64,
                token_prices: vec![TokenPrice {
                    token: feed.token,
                    prices: vec![TokenAmount {
                        currency: feed.currency,
                        amount: feed.amount,
                    }]
                    .into(),
                }]
                .into(),
            };
            let ctx = &Context {
                block: block_context,
                tx: transaction,
                tx_idx: 2,
            };
            let result = set_oracle.index(&services, ctx);
        }

        // for feed_1 in feeds_1 {
        //     let oracle_token_currency = OracleTokenCurrency {
        //         id: feed_1.key,
        //         key: (feed_1.token.to_owned(), feed_1.currency.to_owned()),

        //         token: feed_1.token.to_owned(),
        //         currency: feed_1.currency.to_owned(),
        //         oracle_id: feed_1.oracle_id,
        //         weightage: 1,
        //         block: feed_1.block.clone(),
        //     };

        //     services
        //         .oracle_token_currency
        //         .by_id
        //         .put(&oracle_token_currency.id, &oracle_token_currency)
        //         .expect("Failed to save oracle_token_currency");

        //     let block_context = BlockContext {
        //         hash: feed_1.block.hash,
        //         height: feed_1.block.height,
        //         time: feed_1.block.time,
        //         median_time: feed_1.block.median_time,
        //     };

        //     let transaction = Transaction {
        //         txid: feed_1.txid,
        //         hash: feed_1.block.hash.to_string(),
        //         version: 1,
        //         size: 100,
        //         vsize: 90,
        //         weight: 80,
        //         locktime: 0,
        //         vin: vec![],
        //         vout: vec![],
        //         hex: "transaction_hex".to_string(),
        //     };

        //     let set_oracle = SetOracleData {
        //         oracle_id: feed_1.oracle_id,
        //         timestamp: feed_1.time as i64,
        //         token_prices: vec![TokenPrice {
        //             token: feed_1.token,
        //             prices: vec![TokenAmount {
        //                 currency: feed_1.currency,
        //                 amount: feed_1.amount,
        //             }]
        //             .into(),
        //         }]
        //         .into(),
        //     };
        //     let ctx = &Context {
        //         block: block_context,
        //         tx: transaction,
        //         tx_idx: 2,
        //     };
        //     let result = set_oracle.index(&services, ctx);
        // }
        // for feed_2 in feeds_2 {
        //     let oracle_token_currency = OracleTokenCurrency {
        //         id: feed_2.key,
        //         key: (feed_2.token.to_owned(), feed_2.currency.to_owned()),

        //         token: feed_2.token.to_owned(),
        //         currency: feed_2.currency.to_owned(),
        //         oracle_id: feed_2.oracle_id,
        //         weightage: 1,
        //         block: feed_2.block.clone(),
        //     };

        //     services
        //         .oracle_token_currency
        //         .by_id
        //         .put(&oracle_token_currency.id, &oracle_token_currency)
        //         .expect("Failed to save oracle_token_currency");

        //     let block_context = BlockContext {
        //         hash: feed_2.block.hash,
        //         height: feed_2.block.height,
        //         time: feed_2.block.time,
        //         median_time: feed_2.block.median_time,
        //     };

        //     let transaction = Transaction {
        //         txid: feed_2.txid,
        //         hash: feed_2.block.hash.to_string(),
        //         version: 1,
        //         size: 100,
        //         vsize: 90,
        //         weight: 80,
        //         locktime: 0,
        //         vin: vec![],
        //         vout: vec![],
        //         hex: "transaction_hex".to_string(),
        //     };

        //     let set_oracle: SetOracleData = SetOracleData {
        //         oracle_id: feed_2.oracle_id,
        //         timestamp: feed_2.time as i64,
        //         token_prices: vec![TokenPrice {
        //             token: feed_2.token,
        //             prices: vec![TokenAmount {
        //                 currency: feed_2.currency,
        //                 amount: feed_2.amount,
        //             }]
        //             .into(),
        //         }]
        //         .into(),
        //     };
        //     let ctx = &Context {
        //         block: block_context,
        //         tx: transaction,
        //         tx_idx: 2,
        //     };
        //     let result = set_oracle.index(&services, ctx);
        // }
        let result = services
            .price_ticker
            .by_id
            .get(&("TA".to_string(), "USD".to_string()));
        println!("{:?}", result);
    }

    #[test]
    fn test_index_appoint_oracle() {
        let temp_dir = tempdir().expect("Failed to create temporary directory");
        let path = temp_dir.path();
        let ocean_store_result = ocean_store::OceanStore::new(path);
        let ocean_store = match ocean_store_result {
            Ok(ocean_store) => Arc::new(ocean_store),
            Err(error) => {
                panic!("Failed to create OceanStore: {}", error);
            }
        };

        let services = Arc::new(Services::new(ocean_store));

        let oracle = Oracle {
            id: Txid::from_str(
                "0x33f23658be827bd0f23a48c8db205fcf275dcf666d63cbd3e06089decea217d5",
            )
            .unwrap(),
            owner_address: "a9149cd82492b8fadde74486ecebdb497934492b061187".to_string(),
            weightage: 1,
            price_feeds: vec![
                PriceFeedsItem {
                    token: "TA".to_string(),
                    currency: "USD".to_string(),
                },
                PriceFeedsItem {
                    token: "TB".to_string(),
                    currency: "USD".to_string(),
                },
                PriceFeedsItem {
                    token: "TC".to_string(),
                    currency: "USD".to_string(),
                },
                PriceFeedsItem {
                    token: "TD".to_string(),
                    currency: "USD".to_string(),
                },
            ],
            block: BlockContext {
                hash: BlockHash::from_str(
                    "0x84ac36b14a377fa47ed062e06fe949c448b34e543a98d6a00b8738e9d1cd27f1",
                )
                .unwrap(),
                height: 121,
                time: 1714980074,
                median_time: 1714980074,
            },
        };
        services.oracle.by_id.put(&oracle.id, &oracle);

        let block_context = BlockContext {
            hash: BlockHash::from_str(
                "0000000000000000000076b72aeedd65368d07d945a6321916a7e8fc4e3babb0",
            )
            .unwrap(),
            height: 10,
            time: 123456789,
            median_time: 123456789,
        };

        let transaction = Transaction {
            txid: Txid::from_str(
                "f42b38ac12e3fafc96ba1a9ba70cbfe326744aef75df5fb9db5d6e2855ca415f",
            )
            .unwrap(),
            hash: "transaction_hash".to_string(),
            version: 1,
            size: 100,
            vsize: 90,
            weight: 80,
            locktime: 0,
            vin: vec![],
            vout: vec![],
            hex: "transaction_hex".to_string(),
        };

        let appoint_oracle = AppointOracle {
            script: ScriptBuf::default(), // Provide a valid script
            weightage: 1,
            price_feeds: CompactVec::from(vec![
                CurrencyPair {
                    token: "BTC".to_string(),
                    currency: "USD".to_string(),
                },
                CurrencyPair {
                    token: "ETH".to_string(),
                    currency: "USD".to_string(),
                },
            ]),
        };
        let ctx = &Context {
            block: block_context,
            tx: transaction,
            tx_idx: 2,
        };
        let result = appoint_oracle.index(&services, ctx);
        assert!(result.is_ok());
        // Add assertions to check if data is stored correctly in the database
    }
}
