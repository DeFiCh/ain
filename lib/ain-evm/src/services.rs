use std::{
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread::{self, JoinHandle},
};

use parking_lot::Mutex;

use anyhow::Result;
use jsonrpsee_server::ServerHandle as HttpServerHandle;
use tokio::{
    runtime::{Builder, Handle as AsyncHandle},
    sync::mpsc::{self, Sender},
};

use crate::{evm::EVMServices, storage::traits::FlushableStorage};

// TODO: SERVICES needs to go into its own core crate now,
// and this crate be dedicated to evm
// Note: This cannot just move to rs-exports, since rs-exports cannot cannot have reverse
// deps that depend on it.

// Global flag indicating SERVICES initialization
//
// This AtomicBool is necessary to check if SERVICES are initialized, without
// inadvertently initializing them. It prevents unnecessary shutdown operations on uninitialized SERVICES.
pub static IS_SERVICES_INIT_CALL: AtomicBool = AtomicBool::new(false);

lazy_static::lazy_static! {
    // Global services exposed by the library
    pub static ref SERVICES: Services = {
        IS_SERVICES_INIT_CALL.store(true, Ordering::SeqCst);
        Services::new()
    };
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
                log::info!("Starting tokio waiter");
                r.block_on(async move {
                    rx.recv().await;
                });
            }))),
            json_rpc: Mutex::new(None),
            evm: Arc::new(EVMServices::new().expect("Error initializating handlers")),
        }
    }

    pub fn stop_network(&self) -> Result<()> {
        let mut json_rpc_handle = self.json_rpc.lock();
        if (json_rpc_handle).is_none() {
            // Server was never started
            return Ok(());
        }
        json_rpc_handle
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
            .take()
            .expect("runtime terminated?")
            .join()
            .unwrap();

        // Persist EVM State to disk
        self.evm.core.flush().expect("Could not flush evm state");
        self.evm.storage.flush().expect("Could not flush storage");
    }
}
