use ain_evm_state::handler::EVMHandler;
use ain_evm_state::traits::PersistentState;
use ain_evm_state::EVM_STATE_PATH;
use jsonrpsee_http_server::HttpServerHandle;
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use tokio::runtime::{Builder, Handle as AsyncHandle};
use tokio::sync::mpsc::{self, Sender};

lazy_static::lazy_static! {
    // Global runtime exposed by the library
    pub static ref RUNTIME: Runtime = Runtime::new();
}

pub struct Runtime {
    pub rt_handle: AsyncHandle,
    pub tx: Sender<()>,
    pub handle: Mutex<Option<JoinHandle<()>>>,
    pub jrpc_handle: Mutex<Option<HttpServerHandle>>, // dropping the handle kills server
    pub evm: Arc<EVMHandler>,
}

impl Runtime {
    pub fn new() -> Self {
        let r = Builder::new_multi_thread().enable_all().build().unwrap();
        let (tx, mut rx) = mpsc::channel(1);
        Runtime {
            tx,
            rt_handle: r.handle().clone(),
            handle: Mutex::new(Some(thread::spawn(move || {
                log::info!("Starting runtime in a separate thread");
                r.block_on(async move {
                    rx.recv().await;
                });
            }))),
            jrpc_handle: Mutex::new(None),
            evm: Arc::new(EVMHandler::new()),
        }
    }

    pub fn stop(&self) {
        let _ = self.tx.blocking_send(());
        self.handle
            .lock()
            .unwrap()
            .take()
            .expect("runtime terminated?")
            .join()
            .unwrap();

        // Persist EVM State to disk
        self.evm
            .state
            .write()
            .unwrap()
            .save_to_disk(EVM_STATE_PATH)
            .unwrap();
    }
}
