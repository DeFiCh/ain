#[macro_use]
extern crate serde;
extern crate serde_json;

mod codegen;

use env_logger::{Builder as LogBuilder, Env};
use jsonrpsee_core::server::rpc_module::Methods;
use jsonrpsee_http_server::{HttpServerBuilder, HttpServerHandle};
use log::Level;
use tokio::runtime::{Builder, Handle as AsyncHandle};
use tokio::sync::mpsc::{self, Sender};
use tonic::transport::Server;

use crate::codegen::rpc::{BlockchainService, EthService, Client};

use std::collections::HashMap;
use std::error::Error;
use std::net::SocketAddr;
use std::sync::{Mutex, RwLock};
use std::thread::{self, JoinHandle};

lazy_static::lazy_static! {
    // RPC clients cached globally based on address so that clients can be instantiated at will
    static ref CLIENTS: RwLock<HashMap<String, Client>> = RwLock::new(HashMap::new());
    // Global runtime exposed by the library
    static ref RUNTIME: Runtime = Runtime::new();
}

struct Runtime {
    rt_handle: AsyncHandle,
    tx: Sender<()>,
    handle: Mutex<Option<JoinHandle<()>>>,
    jrpc_handle: Mutex<Option<HttpServerHandle>>, // dropping the handle kills server
}

impl Runtime {
    fn new() -> Self {
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
        }
    }

    fn add_json_rpc_server(&self, addr: &str) -> Result<(), Box<dyn Error>> {
        log::info!("Starting JSON RPC server at {}", addr);
        let addr = addr.parse::<SocketAddr>()?;
        let handle = self.rt_handle.clone();
        let server = self.rt_handle.block_on(
            HttpServerBuilder::default()
                .custom_tokio_runtime(handle)
                .build(addr),
        )?;
        let mut methods: Methods = Methods::new();
        methods.merge(BlockchainService::module()?)?;
        methods.merge(EthService::module()?)?;

        *self.jrpc_handle.lock().unwrap() = Some(server.start(methods)?);
        Ok(())
    }

    fn add_grpc_server(&self, addr: &str) -> Result<(), Box<dyn Error>> {
        log::info!("Starting gRPC server at {}", addr);
        self.rt_handle.spawn(
            Server::builder()
                .add_service(BlockchainService::service())
                .add_service(EthService::service())
                .serve(addr.parse()?),
        );
        Ok(())
    }

    fn stop(&self) {
        let _ = self.tx.blocking_send(());
        self.handle
            .lock()
            .unwrap()
            .take()
            .expect("runtime terminated?")
            .join()
            .unwrap();
    }
}

#[cxx::bridge]
mod server {
    extern "Rust" {
        fn init_runtime();

        fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<()>;

        fn stop_runtime();
    }
}

fn init_runtime() {
    log::info!("Starting gRPC and JSON RPC servers");
    LogBuilder::from_env(Env::default().default_filter_or(Level::Info.as_str())).init();
    let _ = &*RUNTIME;
}

fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<(), Box<dyn Error>> {
    RUNTIME.add_json_rpc_server(json_addr)?;
    RUNTIME.add_grpc_server(grpc_addr)?;
    Ok(())
}

fn stop_runtime() {
    log::info!("Stopping gRPC and JSON RPC servers");
    RUNTIME.stop();
}
