pub mod eth;
pub mod params;
pub mod sync_status;

use ethereum_types::H256;
use jsonrpsee::core::traits::IdProvider;
use serde::{Serialize, Serializer};

use crate::{block::RpcBlockHeader, logs::LogResult, subscription::sync_status::PubSubSyncStatus};

/// Subscription result.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PubSubResult {
    /// New block header.
    Header(Box<RpcBlockHeader>),
    /// Log
    Log(Box<LogResult>),
    /// Transaction hash
    TransactionHash(H256),
    /// SyncStatus
    SyncState(PubSubSyncStatus),
}

impl Serialize for PubSubResult {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            PubSubResult::Header(ref header) => header.serialize(serializer),
            PubSubResult::Log(ref log) => log.serialize(serializer),
            PubSubResult::TransactionHash(ref hash) => hash.serialize(serializer),
            PubSubResult::SyncState(ref sync) => sync.serialize(serializer),
        }
    }
}

#[derive(Debug, Default)]
pub struct MetachainSubIdProvider;

impl IdProvider for MetachainSubIdProvider {
    fn next_id(&self) -> jsonrpsee::types::SubscriptionId<'static> {
        format!("0x{}", hex::encode(rand::random::<u128>().to_le_bytes())).into()
    }
}
