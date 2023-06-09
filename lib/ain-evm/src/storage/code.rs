use primitive_types::{H256, U256};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

use super::traits::PersistentState;

/// `CodeHistory` maintains a history of accounts' codes.
///
/// It tracks the current state (`code_map`), as well as a history (`history`) of code hashes
/// that should be removed if a specific block is rolled back. The correct account code_hash
/// is tracked by the state trie.
/// This structure is solely required for rolling back and preventing ghost entries.
#[derive(Default, Debug, Serialize, Deserialize)]
pub struct CodeHistory {
    /// The current state of each code
    code_map: HashMap<H256, Vec<u8>>,
    /// A map from block number to a vector of code hashes to remove for that block.
    history: HashMap<U256, Vec<H256>>,
}

impl PersistentState for CodeHistory {}

impl CodeHistory {
    pub fn insert(&mut self, block_number: U256, code_hash: H256, code: Vec<u8>) {
        self.code_map.insert(code_hash, code.clone());
        self.history
            .entry(block_number)
            .or_insert_with(Vec::new)
            .push(code_hash);
    }

    pub fn get(&self, code_hash: &H256) -> Option<&Vec<u8>> {
        self.code_map.get(code_hash)
    }

    pub fn rollback(&mut self, block_number: U256) {
        if let Some(code_hashes) = self.history.remove(&block_number) {
            for code_hash in &code_hashes {
                self.code_map.remove(code_hash);
            }
        }
    }
}
