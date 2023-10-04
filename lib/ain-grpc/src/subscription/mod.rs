use anyhow::format_err;
use std::sync::Arc;

use crate::block::RpcBlockHeader;
use ain_evm::log::Notification;
use ain_evm::{evm::EVMServices, storage::traits::BlockStorage};
use ethereum_types::{H160, H256, U256};
use jsonrpsee::core::traits::IdProvider;
use jsonrpsee::{proc_macros::rpc, types::SubscriptionEmptyError, SubscriptionSink};
use log::debug;
use serde::de::Error;
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use serde_json::{from_value, Value};
use serde_with::{serde_as, OneOrMany};

use crate::receipt::LogResult;

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
    pub starting_block: U256,
    pub current_block: U256,
    #[serde(default = "Default::default", skip_serializing_if = "Option::is_none")]
    pub highest_block: Option<U256>,
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

#[serde_as]
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct LogsSubscriptionParams {
    #[serde_as(as = "Option<OneOrMany<_>>")]
    pub address: Option<Vec<H160>>,
    pub topics: Option<Vec<Option<H256>>>,
}

/// Subscription kind.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Default)]
pub enum Params {
    /// No parameters passed.
    #[default]
    None,
    // Log parameters.
    Logs(LogsSubscriptionParams),
}

impl<'a> Deserialize<'a> for Params {
    fn deserialize<D>(deserializer: D) -> Result<Params, D::Error>
    where
        D: Deserializer<'a>,
    {
        let v: Value = Deserialize::deserialize(deserializer)?;

        if v.is_null() {
            return Ok(Params::None);
        }

        from_value(v)
            .map(Params::Logs)
            .map_err(|e| Error::custom(format!("Invalid logs parameters: {}", e)))
    }
}

#[derive(Debug, Default)]
pub struct MetachainSubIdProvider;

impl IdProvider for MetachainSubIdProvider {
    fn next_id(&self) -> jsonrpsee::types::SubscriptionId<'static> {
        format!("0x{}", hex::encode(rand::random::<u128>().to_le_bytes())).into()
    }
}

/// Metachain WebSockets interface.
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
    handler: Arc<EVMServices>,
}

impl MetachainPubSubModule {
    #[must_use]
    pub fn new(handler: Arc<EVMServices>) -> Self {
        Self { handler }
    }
}

impl MetachainPubSubServer for MetachainPubSubModule {
    fn subscribe(
        &self,
        mut sink: SubscriptionSink,
        kind: Kind,
        params: Option<Params>,
    ) -> Result<(), SubscriptionEmptyError> {
        debug!(target: "pubsub", "subscribing to {:#?}", kind);
        debug!(target: "pubsub", "params {:?}", params);
        sink.accept()?;

        let handler = self.handler.clone();

        let fut = async move {
            match kind {
                Kind::NewHeads => {
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        if let Notification::Block(hash) = notification {
                            if let Some(block) = handler.storage.get_block_by_hash(&hash)? {
                                let _ =
                                    sink.send(&PubSubResult::Header(Box::new(block.header.into())));
                                debug!(target: "pubsub", "Received block hash in newHeads: {:x?}", hash);
                            }
                        }
                    }
                }
                Kind::Logs => {
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        if let Notification::Block(hash) = notification {
                            if let Some(block) = handler.storage.get_block_by_hash(&hash)? {
                                let logs = match &params {
                                    Some(Params::Logs(p)) => handler.logs.get_logs(
                                        &p.address,
                                        &p.topics,
                                        block.header.number,
                                    )?,
                                    _ => {
                                        handler.logs.get_logs(&None, &None, block.header.number)?
                                    }
                                };

                                for log in logs {
                                    let _ = sink.send(&PubSubResult::Log(Box::new(log.into())));
                                }
                            }
                        }
                    }
                }
                Kind::NewPendingTransactions => {
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        if let Notification::Transaction(hash) = notification {
                            debug!(target: "pubsub",
                                "Received transaction hash in newPendingTransactions: {:x?}",
                                hash
                            );
                            let _ = sink.send(&PubSubResult::TransactionHash(hash));
                        }
                    }
                }
                Kind::Syncing => {
                    let is_syncing = || -> Result<PubSubSyncStatus, anyhow::Error> {
                        match ain_cpp_imports::get_sync_status() {
                            Ok((current, highest)) => {
                                if current != highest {
                                    // Convert to EVM block number
                                    let current_block = handler
                                        .storage
                                        .get_latest_block()?
                                        .ok_or(format_err!("Unable to find latest block"))?
                                        .header
                                        .number;
                                    let starting_block = handler.block.get_starting_block_number();
                                    let highest_block = current_block + (highest - current);

                                    Ok(PubSubSyncStatus::Detailed(SyncStatusMetadata {
                                        syncing: current != highest,
                                        starting_block,
                                        current_block,
                                        highest_block: Some(highest_block),
                                    }))
                                } else {
                                    Ok(PubSubSyncStatus::Simple(false))
                                }
                            }
                            Err(_) => Ok(PubSubSyncStatus::Simple(false)),
                        }
                    };

                    let mut last_syncing_status = is_syncing()?;
                    let _ = sink.send(&PubSubResult::SyncState(last_syncing_status.clone()));
                    while let Some(notification) = handler.channel.1.write().await.recv().await {
                        let Notification::Block(_) = notification else {
                            continue;
                        };

                        debug!(target: "pubsub", "Received message in sync");
                        let sync_status = is_syncing()?;
                        if sync_status != last_syncing_status {
                            let _ = sink.send(&PubSubResult::SyncState(sync_status.clone()));
                            last_syncing_status = sync_status;
                        }
                    }
                }
            }

            Ok::<(), anyhow::Error>(())
        };
        tokio::task::spawn(fut);
        Ok(())
    }
}
