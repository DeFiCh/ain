pub mod eth;

use ethereum_types::{H160, H256, U256};
use jsonrpsee::core::traits::IdProvider;
use serde::{Deserialize, Serialize, Serializer};
use serde_with::{serde_as, OneOrMany};

use crate::{block::RpcBlockHeader, logs::LogResult};

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SyncStatus {
    pub syncing: bool,
    pub starting_block: U256,
    pub current_block: U256,
    pub highest_block: U256,
}

/// Subscription kind.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub enum Subscription {
    /// New block headers subscription.
    NewHeads,
    /// Logs subscription.
    Logs,
    /// New Pending Transactions subscription.
    NewPendingTransactions,
    /// Node syncing status subscription.
    Syncing,
}

#[serde_as]
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct SubscriptionParams {
    #[serde_as(as = "Option<OneOrMany<_>>")]
    pub address: Option<Vec<H160>>,
    pub topics: Option<SubscriptionParamsTopics>,
}

#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(untagged)]
pub enum SubscriptionParamsTopics {
    VecOfHashes(Vec<Option<H256>>),
    VecOfHashVecs(Vec<Vec<Option<H256>>>),
}

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
    SyncState(SyncStatus),
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
