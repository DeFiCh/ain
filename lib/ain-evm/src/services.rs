use std::{
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::Duration,
};

use anyhow::Result;
use jsonrpsee_server::ServerHandle;
use parking_lot::{Mutex, RwLock};
use tokio::runtime::Runtime;

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
    tokio_runtime: RwLock<Option<Runtime>>,
    pub json_rpc_handles: Mutex<Vec<ServerHandle>>,
    pub websocket_handles: Mutex<Vec<ServerHandle>>,
    pub ocean_handle: Mutex<Option<tokio::task::JoinHandle<()>>>,
    pub evm: Arc<EVMServices>,
}

impl Default for Services {
    fn default() -> Self {
        Self::new()
    }
}

impl Services {
    pub fn new() -> Self {
        let runtime = Runtime::new().expect("Failed to create Tokio runtime");

        Services {
            tokio_runtime: RwLock::new(Some(runtime)),
            json_rpc_handles: Mutex::new(vec![]),
            websocket_handles: Mutex::new(vec![]),
            ocean_handle: Mutex::new(None),
            evm: Arc::new(EVMServices::new().expect("Error initializing handlers")),
        }
    }

    pub fn runtime(&self) -> impl std::ops::Deref<Target = Runtime> + '_ {
        parking_lot::RwLockReadGuard::map(self.tokio_runtime.read(), |opt| {
            opt.as_ref().expect("Runtime has been shut down")
        })
    }
    pub fn stop_network(&self) -> Result<()> {
        for handles in [&self.json_rpc_handles, &self.websocket_handles] {
            let mut handles = handles.lock();
            for server in handles.drain(..) {
                server.stop()?;
            }
        }

        if let Some(handle) = self.ocean_handle.lock().take() {
            handle.abort();
        }

        Ok(())
    }

    pub fn stop(&self) -> Result<()> {
        // Persist EVM State to disk
        self.evm.core.flush()?;
        self.evm.storage.flush()?;

        if let Some(runtime) = self.tokio_runtime.write().take() {
            runtime.shutdown_timeout(Duration::from_secs(10));
        }

        Ok(())
    }
}
