use std::sync::Arc;

use ain_evm::{
    evm::EVMServices, filters::FilterCriteria, storage::traits::BlockStorage,
    subscription::Notification,
};
use anyhow::format_err;
use ethereum_types::U256;
use jsonrpsee::{proc_macros::rpc, types::SubscriptionEmptyError, SubscriptionSink};
use log::{debug, trace};
use tokio::runtime::Handle as AsyncHandle;

use crate::subscription::{
    PubSubResult, Subscription, SubscriptionParams, SubscriptionParamsTopics, SyncStatus,
};

/// Metachain WebSockets interface.
#[rpc(server, namespace = "eth")]
pub trait MetachainPubSub {
    /// Subscribe to Eth subscription.
    #[subscription(
    name = "subscribe" => "subscription",
    unsubscribe = "unsubscribe",
    item = Result,
    )]
    fn subscribe(&self, subscription: Subscription, params: Option<SubscriptionParams>);
}

pub struct MetachainPubSubModule {
    handler: Arc<EVMServices>,
    tokio_runtime: AsyncHandle,
}

impl MetachainPubSubModule {
    #[must_use]
    pub fn new(handler: Arc<EVMServices>, tokio_runtime: AsyncHandle) -> Self {
        Self {
            handler,
            tokio_runtime,
        }
    }
}

impl MetachainPubSubServer for MetachainPubSubModule {
    fn subscribe(
        &self,
        mut sink: SubscriptionSink,
        subscription: Subscription,
        params: Option<SubscriptionParams>,
    ) -> Result<(), SubscriptionEmptyError> {
        sink.accept()?;
        trace!(target: "pubsub", "subscribing to {:#?}", subscription);
        trace!(target: "pubsub", "params {:?}", params);

        let mut rx = self.handler.subscriptions.tx.subscribe();
        let handler = self.handler.clone();
        match subscription {
            Subscription::NewHeads => {
                let fut = async move {
                    while !sink.is_closed() {
                        if let Notification::Block(hash) = rx.recv().await? {
                            if let Some(block) = handler.storage.get_block_by_hash(&hash)? {
                                if !sink
                                    .send(&PubSubResult::Header(Box::new(block.header.into())))?
                                {
                                    break;
                                }
                            } else {
                                return Err(format_err!(
                                    "failed to retrieve block from storage with block hash: {:x?}",
                                    hash
                                ));
                            }
                        }
                    }
                    debug!("Ws connection ended, thread closing");
                    Ok::<(), anyhow::Error>(())
                };
                self.tokio_runtime.spawn(fut);
            }
            Subscription::Logs => {
                let fut = async move {
                    while !sink.is_closed() {
                        if let Notification::Block(hash) = rx.recv().await? {
                            if let Some(block) = handler.storage.get_block_by_hash(&hash)? {
                                let criteria = params
                                    .as_ref()
                                    .map(|p| {
                                        let topics = p.topics.as_ref().map(|topics| match topics {
                                            SubscriptionParamsTopics::VecOfHashes(inputs) => inputs
                                                .iter()
                                                .flatten()
                                                .map(|input| vec![*input])
                                                .collect(),
                                            SubscriptionParamsTopics::VecOfHashVecs(inputs) => {
                                                inputs
                                                    .iter()
                                                    .map(|hashes| {
                                                        hashes.iter().flatten().copied().collect()
                                                    })
                                                    .collect()
                                            }
                                        });
                                        FilterCriteria {
                                            addresses: p.address.clone(),
                                            topics,
                                            ..Default::default()
                                        }
                                    })
                                    .unwrap_or_default();
                                let logs = handler
                                    .filters
                                    .get_block_logs(&criteria, block.header.number)?;
                                let mut disconnect = false;
                                for log in logs {
                                    if !sink.send(&PubSubResult::Log(Box::new(log.into())))? {
                                        disconnect = true;
                                        break;
                                    }
                                }
                                if disconnect {
                                    break;
                                }
                            } else {
                                return Err(format_err!(
                                    "failed to retrieve block from storage with block hash: {:x?}",
                                    hash
                                ));
                            }
                        }
                    }
                    debug!("Ws connection ended, thread closing");
                    Ok::<(), anyhow::Error>(())
                };
                self.tokio_runtime.spawn(fut);
            }
            Subscription::NewPendingTransactions => {
                let fut = async move {
                    while !sink.is_closed() {
                        if let Notification::Transaction(hash) = rx.recv().await? {
                            if !sink.send(&PubSubResult::TransactionHash(hash))? {
                                break;
                            }
                        }
                    }
                    debug!("Ws connection ended, thread closing");
                    Ok::<(), anyhow::Error>(())
                };
                self.tokio_runtime.spawn(fut);
            }
            Subscription::Syncing => {
                let fut = async move {
                    let get_sync_status = || -> Result<SyncStatus, anyhow::Error> {
                        let (current, highest) = ain_cpp_imports::get_sync_status()
                            .map_err(|_| format_err!("failed to get sync status"))?;
                        let current_block = handler
                            .storage
                            .get_latest_block()?
                            .ok_or(format_err!("Unable to find latest block"))?
                            .header
                            .number;
                        let diff = U256::from(highest.saturating_sub(current));
                        let highest_block = current_block.saturating_add(diff);
                        Ok(SyncStatus {
                            syncing: current != highest,
                            starting_block: handler.block.get_starting_block_number(),
                            current_block,
                            highest_block,
                        })
                    };
                    let mut last_sync_status = get_sync_status()?;
                    if !last_sync_status.syncing {
                        // Node is at tip. Return false flag once and exit thread
                        let _ = sink.send(&false)?;
                        return Ok::<(), anyhow::Error>(());
                    } else {
                        sink.send(&PubSubResult::SyncState(last_sync_status.clone()))?
                            .then_some(true)
                            .ok_or_else(|| format_err!("send failed, sink closed"))?;
                    }

                    while !sink.is_closed() {
                        if let Notification::Block(_) = rx.recv().await? {
                            let sync_status = get_sync_status()?;
                            if sync_status.current_block != last_sync_status.current_block {
                                if !sync_status.syncing {
                                    // Node sync-ed to tip. Return false flag once and exit thread
                                    let _ = sink.send(&false)?;
                                    break;
                                }
                                if !sink.send(&PubSubResult::SyncState(sync_status.clone()))? {
                                    break;
                                }
                            }
                            last_sync_status = sync_status;
                        }
                    }
                    debug!("Ws connection ended, thread closing");
                    Ok::<(), anyhow::Error>(())
                };
                self.tokio_runtime.spawn(fut);
            }
        }
        Ok(())
    }
}
