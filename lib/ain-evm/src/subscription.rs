use ethereum_types::H256;
use tokio::sync::broadcast::{self, Sender};

use crate::Result;

pub const NOTIFICATION_CHANNEL_BUFFER_SIZE: usize = 10_000;

#[derive(Clone)]
pub enum Notification {
    Block(H256),
    Transaction(H256),
}

pub struct SubscriptionService {
    pub tx: Sender<Notification>,
}

impl SubscriptionService {
    pub fn new() -> Self {
        let (tx, _rx) =
            broadcast::channel(ain_cpp_imports::get_evm_notification_channel_buffer_size());
        Self { tx }
    }

    pub fn send(&self, notification: Notification) -> Result<()> {
        // Do not need to handle send error since there may be no active receivers.
        let _ = self.tx.send(notification);
        Ok(())
    }
}

impl Default for SubscriptionService {
    fn default() -> Self {
        Self::new()
    }
}
