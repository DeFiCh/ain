use primitive_types::U256;
use serde::{Serialize, Serializer};

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct SyncInfo {
    pub starting_block: U256,
    pub current_block: U256,
    pub highest_block: U256,
}

/// Sync state
#[derive(Debug, Deserialize, Clone)]
pub enum SyncState {
    /// Only hashes
    Synced(bool),
    /// Full transactions
    Syncing(SyncInfo),
}

impl Serialize for SyncState {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            SyncState::Synced(ref sync) => sync.serialize(serializer),
            SyncState::Syncing(ref sync_info) => sync_info.serialize(serializer),
        }
    }
}
