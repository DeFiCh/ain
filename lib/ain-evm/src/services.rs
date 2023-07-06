use crate::evm::EVMServices;
use crate::storage::traits::FlushableStorage;

use anyhow::Result;
use jsonrpsee_http_server::HttpServerHandle;
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use tokio::runtime::{Builder, Handle as AsyncHandle};
use tokio::sync::mpsc::{self, Sender};

lazy_static::lazy_static! {
    // Global services exposed by the library
    pub static ref SERVICES: Services = Services::new();
}

pub struct Services {
    pub tokio_runtime: AsyncHandle,
    pub tokio_runtime_channel_tx: Sender<()>,
    pub tokio_worker: Mutex<Option<JoinHandle<()>>>,
    pub json_rpc: Mutex<Option<HttpServerHandle>>,
    pub evm: Arc<EVMServices>,
}

impl Default for Services {
    fn default() -> Self {
        Self::new()
    }
}

impl Services {
    pub fn new() -> Self {
        let r = Builder::new_multi_thread().enable_all().build().unwrap();
        let (tx, mut rx) = mpsc::channel(1);

        Services {
            tokio_runtime_channel_tx: tx,
            tokio_runtime: r.handle().clone(),
            tokio_worker: Mutex::new(Some(thread::spawn(move || {
                log::info!("Starting tokio worker thread");
                r.block_on(async move {
                    rx.recv().await;
                });
            }))),
            json_rpc: Mutex::new(None),
            evm: Arc::new(EVMServices::new().expect("Error initializating handlers")),
        }
    }

    pub fn stop_network(&self) -> Result<()> {
        self.json_rpc
            .lock()
            .unwrap()
            .take()
            .expect("json rpc server not running")
            .stop()
            .unwrap();

        // TODO: Propogate error
        Ok(())
    }

    pub fn stop(&self) {
        let _ = self.tokio_runtime_channel_tx.blocking_send(());

        self.tokio_worker
            .lock()
            .unwrap()
            .take()
            .expect("runtime terminated?")
            .join()
            .unwrap();

        // Persist EVM State to disk
        self.evm.core.flush().expect("Could not flush evm state");
        self.evm.storage.flush().expect("Could not flush storage");
    }
}
