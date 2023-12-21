#[cfg(test)]
mod tests {
    use tempfile::tempdir;
    use tokio::task;

    use crate::{
        data_acces::{transaction::TransactionVinDb, transaction_vout::TransactionVoutDb},
        database::db_manager::{ColumnFamilyOperations, RocksDB},
        model::{
            transaction::{Transaction, TransactionBlock},
            transaction_vout::{TransactionVout, TransactionVoutScript},
        },
    };

    fn setup_test_db() -> TransactionVinDb {
        let temp_dir = tempdir().unwrap();
        let db = RocksDB::new(temp_dir.path().to_str().unwrap()).unwrap();
        TransactionVinDb { db }
    }

    // Sample transaction for testing
    fn sample_transaction(id: &str) -> Transaction {
        Transaction {
            id: id.to_string(),
            order: 1,
            block: TransactionBlock {
                hash: "sample_hash".to_string(),
                height: 123,
                time: 1000,
                median_time: 1000,
            },
            txid: id.to_string(),
            hash: "sample_transaction_hash".to_string(),
            version: 1,
            size: 200,
            v_size: 200,
            weight: 300,
            total_vout_value: "1000".to_string(),
            lock_time: 0,
            vin_count: 2,
            vout_count: 2,
        }
    }

    fn sample_transaction_Vout() {
        // Instantiate a sample TransactionVoutScript
        let sample_script = TransactionVoutScript {
            hex: "76a91488ac".to_string(),    // Sample hex string
            r#type: "pubkeyhash".to_string(), // Sample type
        };

        // Instantiate a sample TransactionVout
        let sample_vout = TransactionVout {
            id: "vout1".to_string(),     // Sample ID
            txid: "tx12345".to_string(), // Sample transaction ID
            n: 0,                        // Sample index
            value: "0.0001".to_string(), // Sample value in BTC or similar currency
            token_id: 123,               // Sample token ID
            script: sample_script,       // Use the sample script created above
        };
    }

    #[tokio::test]
    async fn test_store_transaction() {
        let txn_vin_db = setup_test_db();

        let test_transaction = sample_transaction("tx1");

        let result = txn_vin_db.store(test_transaction).await;
        assert!(result.is_ok());
    }

    #[tokio::test]
    async fn test_store_get_transaction() {
        let txn_vin_db = setup_test_db();

        let test_transaction = sample_transaction("tx1");
        let trx_id = test_transaction.id.clone();

        let result = txn_vin_db.store(test_transaction.clone()).await;
        assert!(result.is_ok());

        let result = txn_vin_db.get(trx_id).await.unwrap().unwrap();
        assert_eq!(test_transaction, result);
    }

    #[tokio::test]
    async fn test_store_get_delete_transaction() {
        let txn_vin_db = setup_test_db();

        let test_transaction = sample_transaction("tx1");
        let trx_id = test_transaction.id.clone();

        let result = txn_vin_db.store(test_transaction.clone()).await;
        assert!(result.is_ok());

        let result = txn_vin_db.get(trx_id.clone()).await.unwrap().unwrap();
        assert_eq!(test_transaction, result);

        let result = txn_vin_db.delete(trx_id).await;
        assert!(result.is_ok());
    }

    #[tokio::test]
    async fn test_query_transaction() {
        let txn_vin_db = setup_test_db();

        let test_transaction = sample_transaction("tx1");
        let trx_id = test_transaction.id.clone();

        let result = txn_vin_db.store(test_transaction.clone()).await;
        assert!(result.is_ok());

        let result = txn_vin_db
            .query_by_block_hash(test_transaction.block.hash.to_string(), 10, 0)
            .await;
        assert!(result.is_ok());
    }
}
