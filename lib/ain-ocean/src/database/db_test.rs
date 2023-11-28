// In your src/lib.rs or a dedicated module for RocksDB interaction

extern crate rocksdb;
extern crate tempdir;
use bitcoin::blockdata::block::Header;
use bitcoin::blockdata::block::Version;
use bitcoin::blockdata::script::ScriptBuf;
use bitcoin::blockdata::transaction::OutPoint;
use bitcoin::blockdata::transaction::Transaction;
use bitcoin::blockdata::transaction::TxIn;
use bitcoin::blockdata::transaction::TxOut;
use bitcoin::blockdata::witness::Witness;
use bitcoin::hash_types::TxMerkleNode;
use bitcoin::hash_types::Txid;
use bitcoin::pow::CompactTarget;
use bitcoin::Block;
use bitcoin::BlockHash;
use bitcoin_hashes::sha256d;
use bitcoin_hashes::Hash;
use hex;
use rocksdb::{Options, DB};

// Function to initialize a RocksDB instance
pub fn init_db(path: &str) -> DB {
    let mut opts = Options::default();
    opts.create_if_missing(true);
    DB::open(&opts, path).expect("failed to open database")
}

pub fn create_mock_header() -> Header {
    // Convert hex string to a byte array

    let hash: BlockHash = BlockHash::from_slice(&[0u8; 32]).unwrap();
    let merkle_root_bytes =
        hex::decode("c9a4892e8ab7704e5078797f74c4d684ff493e22750a528cec21bf30ef73a3b7")
            .expect("Invalid hex string for merkle root");

    // Convert the byte array to a Hash type
    let merkle_root_hash =
        sha256d::Hash::from_slice(&merkle_root_bytes).expect("Invalid bytes for merkle root hash");

    Header {
        version: Version::from_consensus(1527281788),
        prev_blockhash: hash, // Mock previous block hash
        merkle_root: TxMerkleNode::from_raw_hash(merkle_root_hash),
        time: 2236946890,
        bits: CompactTarget::from_consensus(3633759788),
        nonce: 491612868,
    }
}

pub fn create_mock_block() -> Block {
    // Convert hex string to a byte array

    let hash: BlockHash = BlockHash::from_slice(&[0u8; 32]).unwrap();
    let merkle_root_bytes =
        hex::decode("c9a4892e8ab7704e5078797f74c4d684ff493e22750a528cec21bf30ef73a3b7")
            .expect("Invalid hex string for merkle root");

    // Convert the byte array to a Hash type
    let merkle_root_hash =
        sha256d::Hash::from_slice(&merkle_root_bytes).expect("Invalid bytes for merkle root hash");

    let tx_bytes = hex::decode("c9a4892e8ab7704e5078797f74c4d684ff493e22750a528cec21bf30ef73a3b7")
        .expect("Invalid hex string for merkle root");
    let tx_id = sha256d::Hash::from_slice(&tx_bytes).expect("Invalid bytes for merkle root hash");

    let header = Header {
        version: Version::from_consensus(1527281788),
        prev_blockhash: hash, // Mock previous block hash
        merkle_root: TxMerkleNode::from_raw_hash(merkle_root_hash),
        time: 2236946890,
        bits: CompactTarget::from_consensus(3633759788),
        nonce: 491612868,
    };

    let input = TxIn {
        previous_output: OutPoint {
            txid: Txid::from_raw_hash(tx_id),

            vout: 4294967295,
        },
        script_sig: ScriptBuf::new(),
        sequence: bitcoin::Sequence(4294967295),
        witness: Witness::default(),
    };

    let output1 = TxOut {
        value: 5000000000,
        script_pubkey: ScriptBuf::new(),
    };

    let output2 = TxOut {
        value: 0,
        script_pubkey: ScriptBuf::new(),
    };

    // Construct the transaction
    let transaction = Transaction {
        version: 2,
        lock_time: bitcoin::absolute::LockTime::from_height(0).unwrap(),
        input: vec![input],
        output: vec![output1, output2],
    };

    Block {
        header,
        txdata: vec![transaction],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempdir::TempDir;

    #[test]
    fn test_db_headers_store_retrieve() {
        let temp_dir = TempDir::new("rocksdb_test").unwrap();
        let rocks_db = RocksDB::new(temp_dir.path().to_str().unwrap()).unwrap();
        // Create a mock header
        let header = create_mock_header();
        rocks_db.put_block_header(&header).unwrap();
        let bh = header.block_hash().to_string();

        let retrieved_data = rocks_db.get_block_header(bh.as_bytes()).unwrap();

        assert!(
            retrieved_data.is_some(),
            "No data was retrieved for the header"
        );
        let retrieved_header: Header = deserialize(&retrieved_data.unwrap()).unwrap();
        assert_eq!(
            header, retrieved_header,
            "Retrieved header should match the original header"
        );
    }

    #[test]
    fn test_db_block_store_retrieve() {
        let temp_dir = TempDir::new("rocksdb_test").unwrap();
        let rocks_db = RocksDB::new(temp_dir.path().to_str().unwrap()).unwrap();
        // Create a mock header
        let new_block = create_mock_block();
        rocks_db.put_block(&new_block).unwrap();
        let bh = new_block.block_hash().to_string();
        let retrieved_data = rocks_db.get_block(bh.as_bytes()).unwrap();
        assert!(
            retrieved_data.is_some(),
            "No data was retrieved for the header"
        );
        let retrieved_block: Block = deserialize(&retrieved_data.unwrap()).unwrap();
        assert_eq!(
            new_block, retrieved_block,
            "Retrieved header should match the original header"
        );
    }

    #[test]
    fn test_db_latest_block_hash_store_retrieve() {
        let temp_dir = TempDir::new("rocksdb_test").unwrap();
        let rocks_db = RocksDB::new(temp_dir.path().to_str().unwrap()).unwrap();

        // Create and store a test block header
        let header = create_mock_header();
        rocks_db.put_block_header(&header).unwrap();

        // Retrieve the latest block hash from the database
        let stored_latest_block_hash = rocks_db.get_latest_block_hash().unwrap();

        // Check that the retrieved hash matches the test header's block hash
        assert_eq!(
            Some(header.block_hash().to_string()),
            stored_latest_block_hash,
            "The retrieved latest block hash does not match the expected value."
        );
    }
}
