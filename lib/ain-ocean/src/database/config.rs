use std::{env, fs, path::PathBuf};

use serde::{Deserialize, Serialize};
use structopt::StructOpt;

#[derive(Debug, Clone, Serialize, Deserialize, StructOpt)]
pub struct Config {
    /// Path to the RocksDB database on the local file system.
    #[structopt(
        long = "rocksdb-path",
        env = "ROCKSDB_PATH",
        default_value = "rocksdb/data"
    )]
    pub rocksdb_path: String,
}
impl Default for Config {
    fn default() -> Self {
        let mut path = match env::var("HOME") {
            Ok(val) => PathBuf::from(val),
            Err(_) => panic!("Couldn't find a home directory"),
        };
        path.push("rocksdb/data");
        if let Err(e) = fs::create_dir_all(&path) {
            panic!("Failed to create default RocksDB path: {}", e);
        }

        let path_str = match path.to_str() {
            Some(p) => p,
            None => panic!("Failed to convert path to string"),
        };

        Self {
            rocksdb_path: path_str.to_string(),
        }
    }
}
