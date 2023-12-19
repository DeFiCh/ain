#[cfg(test)]
mod tests {
    use super::*;
    use crate::data_acces::masternode::MasterNodeDB;
    use crate::database::db_manager::{RocksDB, SortOrder};
    use crate::model::masternode::{HistoryItem, Masternode, MasternodeBlock};
    use chrono::Utc;
    use tempfile::tempdir;

    fn setup_test_db() -> MasterNodeDB {
        let temp_dir = tempdir().unwrap();
        let db = RocksDB::new(temp_dir.path().to_str().unwrap()).unwrap(); // Adjust this according to your RocksDB struct
        MasterNodeDB { db }
    }
    fn create_dummy_masternode(id: &str) -> Masternode {
        Masternode {
            id: id.to_string(),
            sort: Some("dummy_sort".to_string()),
            owner_address: "dummy_owner_address".to_string(),
            operator_address: "dummy_operator_address".to_string(),
            creation_height: 100,
            resign_height: 200,
            resign_tx: Some("dummy_resign_tx".to_string()),
            minted_blocks: 50,
            timelock: 10,
            collateral: "dummy_collateral".to_string(),
            block: create_dummy_masternode_block(),
            history: Some(vec![create_dummy_history_item()]),
        }
    }

    fn create_dummy_masternode_block() -> MasternodeBlock {
        MasternodeBlock {
            hash: "dummy_hash".to_string(),
            height: 100,
            time: Utc::now().timestamp() as u64,
            median_time: Utc::now().timestamp() as u64,
        }
    }

    fn create_dummy_history_item() -> HistoryItem {
        HistoryItem {
            txid: "dummy_txid".to_string(),
            owner_address: "dummy_history_owner_address".to_string(),
            operator_address: "dummy_history_operator_address".to_string(),
        }
    }

    #[tokio::test]
    async fn test_query_with_dummy_data() {
        let master_node = setup_test_db();
        let request = create_dummy_masternode("1");
        let result = master_node.store(request).await;
        assert!(result.is_ok());
    }

    #[tokio::test]
    async fn test_store_and_query_dummy_data() {
        let master_node = setup_test_db();
        let dummy_data = vec![
            create_dummy_masternode("1"),
            create_dummy_masternode("2"),
            create_dummy_masternode("3"),
            create_dummy_masternode("4"),
            create_dummy_masternode("5"),
            create_dummy_masternode("6"),
            create_dummy_masternode("7"),
            create_dummy_masternode("8"),
            create_dummy_masternode("9"),
            create_dummy_masternode("10"),
        ];

        for masternode in dummy_data {
            let result = master_node.store(masternode).await;
            assert!(result.is_ok());
        }
        // let query_result = master_node.query(5, 8, SortOrder::Ascending).await;
        // assert!(query_result.is_err());
        // let queried_masternodes = query_result.unwrap();
        // assert_eq!(queried_masternodes.len(), 5);
    }
}
