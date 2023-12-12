#[cfg(test)]
mod tests {
    use super::*;
    use crate::data_acces::block::BlockDb;
    use crate::database::db_manger::SortOrder;
    use crate::database::db_manger::{ColumnFamilyOperations, RocksDB};
    use crate::model::block::Block;
    use tempdir::TempDir;

    pub fn create_dummy_block(height: i32) -> Block {
        Block {
            id: "1".to_string(),
            hash: "block_hash_1".to_string(),
            previous_hash: "previous_block_hash".to_string(),
            height: height,
            version: 1,
            time: 1634054400,        // Replace with an actual timestamp
            median_time: 1634054400, // Replace with an actual timestamp
            transaction_count: 10,
            difficulty: 12345,
            masternode: "masternode_address".to_string(),
            minter: "minter_address".to_string(),
            minter_block_count: 5,
            reward: "10.0".to_string(),
            stake_modifier: "stake_modifier_value".to_string(),
            merkleroot: "merkleroot_value".to_string(),
            size: 2000,
            size_stripped: 1800,
            weight: 1900,
        }
    }

    #[tokio::test]
    async fn test_put_get_by_hash_and_height() {
        // Create a temporary RocksDB instance for testing
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Create a dummy block for testing
        let dummy_block = create_dummy_block(1);

        // Test the put_block method
        block_db.put_block(dummy_block.clone()).await.unwrap();

        // Test the get_by_hash method
        let result_by_hash = block_db
            .get_by_hash(dummy_block.hash.clone())
            .await
            .unwrap();
        assert_eq!(result_by_hash.unwrap(), dummy_block.clone());

        // Test the get_by_height method
        let result_by_height = block_db.get_by_height(dummy_block.height).await.unwrap();
        assert_eq!(result_by_height.unwrap(), dummy_block);
    }

    #[tokio::test]
    async fn test_get_nonexistent_block_by_hash() {
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Attempt to get a block using a hash that doesn't exist
        let result = block_db
            .get_by_hash("nonexistent_hash".to_string())
            .await
            .unwrap();
        assert_eq!(result, None);
    }

    #[tokio::test]
    async fn test_get_nonexistent_block_by_height() {
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Attempt to get a block using a height that doesn't exist
        let result = block_db.get_by_height(999).await.unwrap();
        assert_eq!(result, None);
    }

    #[tokio::test]
    async fn test_delete_block() {
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Create a dummy block for testing
        let dummy_block = create_dummy_block(1);

        // Put the block
        block_db.put_block(dummy_block.clone()).await.unwrap();

        // Delete the block
        block_db
            .delete_block(dummy_block.hash.clone())
            .await
            .unwrap();

        // Attempt to get the block after deletion
        let result_by_hash = block_db
            .get_by_hash(dummy_block.hash.clone())
            .await
            .unwrap();

        assert!(result_by_hash.is_none());
    }

    #[tokio::test]
    async fn test_query_by_height_descending() {
        // Create a temporary RocksDB instance for testing
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Create dummy blocks with heights 1 to 5 and insert them into the database
        for height in 1..=10 {
            let dummy_block = create_dummy_block(height);
            block_db.put_block(dummy_block.clone()).await.unwrap();
        }

        // Test the query_by_height method
        let result_blocks = block_db
            .query_by_height(3, 4, SortOrder::Descending)
            .await
            .unwrap();
        // Verify that the result contains the expected blocks
        assert_eq!(result_blocks.len(), 3);
        assert_eq!(result_blocks[0].height, 3);
        assert_eq!(result_blocks[1].height, 2);
    }

    #[tokio::test]
    async fn test_query_by_height_ascending() {
        // Create a temporary RocksDB instance for testing
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Create dummy blocks with heights 1 to 5 and insert them into the database
        for height in 1..=10 {
            let dummy_block = create_dummy_block(height);
            block_db.put_block(dummy_block.clone()).await.unwrap();
        }

        // Test the query_by_height method
        let result_blocks = block_db
            .query_by_height(5, 6, SortOrder::Ascending)
            .await
            .unwrap();
        // Verify that the result contains the expected blocks
        assert_eq!(result_blocks.len(), 5);
        assert_eq!(result_blocks[0].height, 1);
        assert_eq!(result_blocks[1].height, 2);
    }

    #[tokio::test]
    async fn test_put_block_updates_latest_height() {
        // Setup test environment
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Create and insert sample blocks
        for height in 1..=10 {
            let dummy_block = create_dummy_block(height);
            block_db.put_block(dummy_block).await.unwrap();
        }

        // Retrieve latest block height
        let latest_height_bytes = block_db
            .db
            .get("latest_block_height", b"latest_block_height")
            .expect("Failed to retrieve latest block height")
            .expect("Latest block height not found");
        let latest_height = i32::from_be_bytes(
            latest_height_bytes
                .as_slice()
                .try_into()
                .expect("Byte length mismatch"),
        );

        // Assert correctness
        assert_eq!(latest_height, 10); // Assert that latest height is 10
    }

    #[tokio::test]
    async fn test_get_highest_retrieves_correct_block() -> Result<(), Box<dyn std::error::Error>> {
        // Setup test environment
        let temp_dir = TempDir::new("test_rocksdb").expect("Failed to create temp directory");
        let rocksdb = RocksDB::new(temp_dir.path().to_str().unwrap())
            .expect("Failed to create RocksDB instance");
        let block_db = BlockDb { db: rocksdb };

        // Insert sample blocks
        for height in 1..=5 {
            let dummy_block = create_dummy_block(height);
            block_db.put_block(dummy_block).await.unwrap();
        }

        // Call get_highest
        let highest_block = block_db.get_highest().await.unwrap().unwrap();

        // Assert that the highest block is the one with height 5
        assert_eq!(highest_block.height, 5);

        Ok(())
    }
}
