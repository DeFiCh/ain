use crate::subscription::params::{Kind, Params};
use crate::subscription::sync_status::{PubSubSyncStatus, SyncStatusMetadata};
use crate::subscription::PubSubResult;
use ain_evm::log::Notification;
use ain_evm::{evm::EVMServices, storage::traits::BlockStorage};
use anyhow::format_err;
use jsonrpsee::{proc_macros::rpc, types::SubscriptionEmptyError, SubscriptionSink};
use log::debug;
use std::sync::Arc;

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
                    while let Some(notification) =
                        handler.channel.receiver.write().await.recv().await
                    {
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
                    while let Some(notification) =
                        handler.channel.receiver.write().await.recv().await
                    {
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
                    while let Some(notification) =
                        handler.channel.receiver.write().await.recv().await
                    {
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
                    while let Some(notification) =
                        handler.channel.receiver.write().await.recv().await
                    {
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
