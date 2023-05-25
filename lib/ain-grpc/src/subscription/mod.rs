use std::sync::Arc;

use ain_evm::{
    handler::{Handlers, Notification},
    storage::traits::{BlockStorage, ReceiptStorage},
};
use ethereum::Header;
use ethereum_types::H256;
use log::debug;
use serde::{de::Error, Deserialize, Deserializer, Serialize, Serializer};
use serde_json::{from_value, Value};

/// Subscription result.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PubSubResult {
    /// New block header.
    Header(Box<Header>),
    /// Log
    Log(Box<LogResult>),
    /// Transaction hash
    TransactionHash(H256),
    /// SyncStatus
    SyncState(PubSubSyncStatus),
}
#[derive(Clone, Debug, Serialize)]
#[serde(untagged)]
pub enum PubSubSyncStatus {
    Simple(bool),
    Detailed(SyncStatusMetadata),
}

impl PartialEq for PubSubSyncStatus {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (PubSubSyncStatus::Simple(s1), PubSubSyncStatus::Simple(s2)) => s1 == s2,
            (PubSubSyncStatus::Detailed(d1), PubSubSyncStatus::Detailed(d2)) => {
                d1.syncing == d2.syncing
            }
            _ => false,
        }
    }
}

impl Eq for PubSubSyncStatus {}

/// PubSbub sync status
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SyncStatusMetadata {
    pub syncing: bool,
    pub starting_block: u64,
    pub current_block: u64,
    #[serde(default = "Default::default", skip_serializing_if = "Option::is_none")]
    pub highest_block: Option<u64>,
}

impl Serialize for PubSubResult {
    fn serialize<S>(&self, serializer: S) -> ::std::result::Result<S::Ok, S::Error>
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

/// Subscription kind.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub enum Kind {
    /// New block headers subscription.
    NewHeads,
    /// Logs subscription.
    Logs,
    /// New Pending Transactions subscription.
    NewPendingTransactions,
    /// Node syncing status subscription.
    Syncing,
}

/// Subscription kind.
#[derive(Clone, Debug, Eq, PartialEq, Hash)]
pub enum Params {
    /// No parameters passed.
    None,
    // Log parameters.
    // Logs(Filter),
}

impl Default for Params {
    fn default() -> Self {
        Params::None
    }
}

impl<'a> Deserialize<'a> for Params {
    fn deserialize<D>(deserializer: D) -> ::std::result::Result<Params, D::Error>
    where
        D: Deserializer<'a>,
    {
        let v: Value = Deserialize::deserialize(deserializer)?;

        if v.is_null() {
            return Ok(Params::None);
        }
        Ok(Params::None)

        // from_value(v)
        //     .map(Params::Logs)
        //     .map_err(|e| D::Error::custom(format!("Invalid Pub-Sub parameters: {}", e)))
    }
}

use jsonrpsee::{proc_macros::rpc, types::SubscriptionEmptyError, SubscriptionSink};

use crate::receipt::{LogResult, ReceiptResult};

/// Metachain PUB-SUB rpc interface.
#[rpc(server)]
pub trait MetachainPubSub {
    /// Subscribe to Eth subscription.
    #[subscription(
		name = "eth_subscribe" => "eth_subscription",
		unsubscribe = "eth_unsubscribe",
		item = Result
	)]
    fn subscribe(&self, kind: Kind, params: Option<Params>);
}

pub struct MetachainPubSubModule {
    handler: Arc<Handlers>,
}

impl MetachainPubSubModule {
    #[must_use]
    pub fn new(handler: Arc<Handlers>) -> Self {
        Self { handler }
    }
}

impl MetachainPubSubServer for MetachainPubSubModule {
    fn subscribe(
        &self,
        mut sink: SubscriptionSink,
        kind: Kind,
        params: Option<Params>,
    ) -> std::result::Result<(), SubscriptionEmptyError> {
        debug!(target: "pubsub", "subscribing to {:#?}", kind);
        sink.accept()?;

        let handler = self.handler.clone();
        let starting_block = handler
            .storage
            .get_latest_block()
            .map_or(0, |b| b.header.number.as_u64());

        let fut = async move {
            match kind {
                Kind::NewHeads => {
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        let Notification::Block(hash) = notification else { continue };
                        let Some(block) = handler.storage.get_block_by_hash(&hash) else { continue };

                        let _ = sink.send(&PubSubResult::Header(Box::new(block.header)));
                        debug!(target: "pubsub", "Received block hash in newHeads: {:x?}", hash);
                    }
                }
                Kind::Logs => {
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        let Notification::Block(hash) = notification else { continue };
                        let Some(block) = handler.storage.get_block_by_hash(&hash) else { continue };

                        debug!(target: "pubsub", "Received block hash in logs: {:x?}", hash);
                        for tx in block.transactions {
                            if let Some(receipt) = handler.storage.get_receipt(&tx.hash()) {
                                let receipt_result = ReceiptResult::from(receipt);
                                for log in receipt_result.logs {
                                    let _ = sink.send(&PubSubResult::Log(Box::new(log)));
                                }
                            }
                        }
                    }
                }
                Kind::NewPendingTransactions => {
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        let Notification::Transaction(hash) = notification else { continue };
                        debug!(target: "pubsub",
                            "Received transaction hash in newPendingTransactions: {:x?}",
                            hash
                        );
                        let _ = sink.send(&PubSubResult::TransactionHash(hash));
                    }
                }
                Kind::Syncing => {
                    let is_syncing = || -> PubSubSyncStatus {
                        match ain_cpp_imports::get_sync_status() {
                            Ok(is_syncing) => {
                                if is_syncing.syncing {
                                    // Convert to EVM block number
                                    let (current_block, highest_block) =
                                        handler.storage.get_latest_block().map_or(
                                            (is_syncing.current_block, is_syncing.highest_block),
                                            |block| {
                                                let diff = is_syncing.highest_block
                                                    - is_syncing.current_block;
                                                (
                                                    block.header.number.as_u64(),
                                                    block.header.number.as_u64() + diff,
                                                )
                                            },
                                        );

                                    PubSubSyncStatus::Detailed(SyncStatusMetadata {
                                        syncing: is_syncing.syncing,
                                        starting_block,
                                        current_block: current_block,
                                        highest_block: Some(highest_block),
                                    })
                                } else {
                                    PubSubSyncStatus::Simple(false)
                                }
                            }
                            Err(_) => PubSubSyncStatus::Simple(false),
                        }
                    };

                    let mut last_syncing_status = is_syncing();
                    let _ = sink.send(&PubSubResult::SyncState(last_syncing_status.clone()));
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        let Notification::Block(_) = notification else { continue };

                        debug!(target: "pubsub", "Received message in sync");
                        let sync_status = is_syncing();
                        if sync_status != last_syncing_status {
                            let _ = sink.send(&PubSubResult::SyncState(sync_status.clone()));
                            last_syncing_status = sync_status;
                        }
                    }
                }
            }
        };
        tokio::task::spawn(fut);
        Ok(())
    }
}
