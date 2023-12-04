use crate::model::oracle::Oracle;
use anyhow::{anyhow, Result};
use bitcoin::blockdata::block::Block;
use bitcoin::blockdata::block::Header;
use bitcoin::consensus::encode::serialize;
use rocksdb::Options;
use rocksdb::{ColumnFamilyDescriptor, IteratorMode, DB};
use serde::Deserialize;
use std::collections::HashSet;
use std::sync::Arc;

#[derive(Debug)]
pub struct RocksDB {
    db: Arc<DB>,
    cfs: HashSet<String>,
}

pub trait ColumnFamilyOperations {
    fn get(&self, cf_name: &str, key: &[u8]) -> Result<Option<Vec<u8>>>;
    fn put(&self, cf_name: &str, key: &[u8], value: &[u8]) -> Result<()>;
    fn delete(&self, cf_name: &str, key: &[u8]) -> Result<()>;
    fn get_total_row(&self) -> Result<()>;
}

impl RocksDB {
    pub fn new(db_path: &str) -> anyhow::Result<RocksDB, anyhow::Error> {
        let mut opts = Options::default();
        opts.create_if_missing(true);
        opts.create_missing_column_families(true);

        let cf_names = [
            "default",
            "block",
            "masternode_stats",
            "masternode",
            "oracle_history",
            "oracle_price_active",
            "oracle_price_aggregated_interval",
            "oracle_price_aggregated",
            "oracle_price_feed",
            "oracle_token_currency",
            "oracle",
            "pool_swap_aggregated",
            "pool_swap",
            "price_ticker",
            "raw_block",
            "script_activity",
            "script_aggregation",
            "script_unspent",
            "transaction",
            "transaction_vin",
            "transaction_vout",
            "vault_auction_history",
            "pool_swap",
        ];
        let mut cf_descriptors = vec![];

        for cf_name in &cf_names {
            let mut cf_opts = Options::default();
            cf_opts.set_max_write_buffer_number(16);
            cf_descriptors.push(ColumnFamilyDescriptor::new(cf_name.to_string(), cf_opts));
        }

        let db = Arc::new(DB::open_cf_descriptors(&opts, db_path, cf_descriptors)?);

        // Keep names of the column families so you can look them up later
        let cfs = cf_names
            .iter()
            .cloned()
            .map(String::from)
            .collect::<HashSet<_>>();

        Ok(Self { db, cfs })
    }

    pub fn put_block(&self, block: &Block) -> anyhow::Result<()> {
        // Serialize the header to a byte vector
        let serialized_header = serialize(block);
        // Convert the block hash to string (Assume it's a suitable key)
        let key = block.block_hash().to_string();
        // Store the block header
        self.put("block", key.as_bytes(), &serialized_header)?;
        Ok(())
    }

    pub fn get_block(&self, key: &[u8]) -> anyhow::Result<Option<Vec<u8>>> {
        self.get("block", key)
    }

    pub fn put_block_header(&self, header: &Header) -> anyhow::Result<()> {
        // Serialize the header to a byte vector
        let serialized_header = serialize(header);
        // Convert the block hash to string (Assume it's a suitable key)
        let key = header.block_hash().to_string();
        // Store the block header
        self.put("block_header", key.as_bytes(), &serialized_header)?;
        // Update the latest block hash
        let latest_block_hash = header.block_hash().to_string();
        self.put_latest_block_hash(
            "latest_block_hash",
            b"latest_block_hash",
            latest_block_hash.as_bytes(),
        )?;

        Ok(())
    }

    pub fn get_block_header(&self, key: &[u8]) -> anyhow::Result<Option<Vec<u8>>> {
        self.get("block_header", key)
    }

    pub fn put_latest_block_hash(
        &self,
        cf_name: &str,
        key: &[u8],
        value: &[u8],
    ) -> anyhow::Result<()> {
        if let Some(cf) = self.cfs.get(cf_name) {
            let cf_handle = self
                .db
                .cf_handle(cf)
                .ok_or_else(|| anyhow::anyhow!("Failed to get column family handle"))?;
            // Key does not exist, proceed to store the new value
            self.db
                .put_cf(cf_handle, key, value)
                .map_err(|_| anyhow::anyhow!("Failed to put key-value pair"))
        } else {
            // Log some diagnostic info here.
            Err(anyhow::anyhow!("Invalid column family name"))
        }
    }

    pub fn count_entries_in_cf(&self, cf_name: &str) -> anyhow::Result<u64> {
        if let Some(cf_name) = self.cfs.get(cf_name) {
            let cf = self
                .db
                .cf_handle(cf_name)
                .ok_or_else(|| anyhow::anyhow!("Could not get column family handle"))?;
            let iter = self.db.iterator_cf(cf, IteratorMode::Start);

            let mut count: u64 = 0;
            for _ in iter {
                count += 1;
            }

            Ok(count)
        } else {
            Err(anyhow::anyhow!("Invalid column family name"))
        }
    }

    pub fn get_latest_block_hash(&self) -> anyhow::Result<Option<String>> {
        let _db_path = self.db.path();
        let cf_name = "latest_block_hash";
        let key = b"latest_block_hash";
        if let Some(cf_name) = self.cfs.get(cf_name) {
            let cf = self
                .db
                .cf_handle(cf_name)
                .ok_or_else(|| anyhow::anyhow!("Failed to get column family handle"))?;

            match self.db.get_cf(cf, key)? {
                Some(value) => {
                    let value_str = String::from_utf8(value)
                        .map_err(|e| anyhow::anyhow!("Failed to convert to UTF-8: {}", e))?;
                    Ok(Some(value_str))
                }
                None => Ok(None),
            }
        } else {
            Err(anyhow::anyhow!("Invalid column family name"))
        }
    }

    //block_hash in block table
    pub async fn block_hash_exists(&self, block_hash: &str) -> anyhow::Result<bool> {
        let cf_name = "block";
        if let Some(cf_name) = self.cfs.get(cf_name) {
            let cf = self
                .db
                .cf_handle(cf_name)
                .ok_or_else(|| anyhow::anyhow!("Failed to get column family handle"))?;

            let key = block_hash.as_bytes();
            match self.db.get_cf(cf, key)? {
                Some(_) => Ok(true), // Block hash exists in the "block" column family
                None => Ok(false),   // Block hash does not exist
            }
        } else {
            Err(anyhow::anyhow!("Invalid column family name"))
        }
    }
}

impl ColumnFamilyOperations for RocksDB {
    fn get(&self, cf_name: &str, key: &[u8]) -> Result<Option<Vec<u8>>> {
        if let Some(cf_name) = self.cfs.get(cf_name) {
            let cf = self
                .db
                .cf_handle(cf_name)
                .expect("Should never fail if column family name is valid");
            let result = self.db.get_cf(cf, key)?;
            Ok(result)
        } else {
            Err(anyhow!("Invalid column family name"))
        }
    }

    fn put(&self, cf_name: &str, key: &[u8], value: &[u8]) -> Result<()> {
        if let Some(cf) = self.cfs.get(cf_name) {
            let cf_handle = self
                .db
                .cf_handle(cf)
                .ok_or_else(|| anyhow!("Failed to get column family handle"))?;

            // Check if the key already exists
            if self.db.get_cf(cf_handle, key)?.is_none() {
                self.db
                    .put_cf(cf_handle, key, value)
                    .map_err(|_| anyhow!("Failed to put key-value pair"))
            } else {
                Ok(())
            }
        } else {
            // Log some diagnostic info here.
            Err(anyhow!("Invalid column family name"))
        }
    }

    fn delete(&self, cf_name: &str, key: &[u8]) -> Result<()> {
        if let Some(cf_name) = self.cfs.get(cf_name) {
            let cf = self
                .db
                .cf_handle(cf_name)
                .expect("Should never fail if column family name is valid");
            self.db.delete_cf(cf, key)?;
            Ok(())
        } else {
            Err(anyhow!("Invalid column family name"))
        }
    }

    fn get_total_row(&self) -> anyhow::Result<()> {
        let db_path = self.db.path();
        println!("{:?}", db_path);

        let block_header_cf = self
            .db
            .cf_handle("block_header")
            .ok_or(anyhow::anyhow!("Column family 'block_header' not found"))?;

        let block_cf = self
            .db
            .cf_handle("block")
            .ok_or(anyhow::anyhow!("Column family 'block' not found"))?;

        let mut block_header_count = 0;
        let mut block_count = 0;

        for _ in self.db.iterator_cf(block_header_cf, IteratorMode::Start) {
            block_header_count += 1;
        }

        // Count rows in "block" column family
        for _ in self.db.iterator_cf(block_cf, IteratorMode::Start) {
            block_count += 1;
        }

        println!("Total rows in 'block_header': {}", block_header_count);
        println!("Total rows in 'block': {}", block_count);
        Ok(())
    }
}
