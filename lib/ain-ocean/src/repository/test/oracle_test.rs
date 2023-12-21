#[cfg(test)]
mod tests {
    use tempfile::tempdir;
    use tokio::task;

    use crate::{
        data_acces::oracle::OracleDb,
        database::db_manager::{ColumnFamilyOperations, RocksDB},
        model::oracle::{Oracle, OracleBlock, PriceFeedsItem},
    };

    // Function to set up a test database environment
    fn setup_test_db() -> OracleDb {
        let temp_dir = tempdir().unwrap();
        let db = RocksDB::new(temp_dir.path().to_str().unwrap()).unwrap(); // Adjust this according to your RocksDB struct
        OracleDb { db }
    }

    // Function to create a dummy Oracle instance for testing
    fn create_dummy_oracle(id: &str) -> Oracle {
        Oracle {
            id: id.to_string(),
            owner_address: "owner_address_example".to_string(),
            weightage: 10,
            price_feeds: vec![PriceFeedsItem {
                token: "token_example".to_string(),
                currency: "currency_example".to_string(),
            }],
            block: OracleBlock {
                hash: "hash_example".to_string(),
                height: 1,
                time: 100000,
                median_time: 100000,
            },
        }
    }

    #[tokio::test]
    async fn test_store() {
        let oracle_db = setup_test_db();
        let oracle = create_dummy_oracle("test_id_1");

        let result = oracle_db.store(oracle).await;
        assert!(result.is_ok());
    }

    #[tokio::test]
    async fn test_get() {
        let oracle_db = setup_test_db();
        let oracle = create_dummy_oracle("test_id_2");
        oracle_db.store(oracle.clone()).await.unwrap();

        let result = oracle_db.get(oracle.id.clone()).await.unwrap();
        assert!(result.is_some());
        println!("the oracle result {:?}", result);
        assert_eq!(result.unwrap(), oracle);
    }

    #[tokio::test]
    async fn test_delete() {
        let oracle_db = setup_test_db();
        let oracle = create_dummy_oracle("test_id_3");
        oracle_db.store(oracle.clone()).await.unwrap();

        let delete_result = oracle_db.delete(oracle.id.clone()).await;
        assert!(delete_result.is_ok());

        let get_result = oracle_db.get(oracle.id).await;
        assert!(get_result.is_ok());
        assert!(get_result.unwrap().is_none());
    }

    // Additional tests for `query` and other edge cases can be added similarly
}
