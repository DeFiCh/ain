#[macro_use]
extern crate serde;
extern crate serde_json;

mod codegen;
pub mod rpc;

use env_logger::{Builder as LogBuilder, Env};
use jsonrpsee_core::server::rpc_module::Methods;
use jsonrpsee_http_server::HttpServerBuilder;
use log::Level;
use tonic::transport::Server;

use crate::codegen::rpc::{BlockchainService, Client, EthService};

use std::collections::HashMap;
use std::error::Error;
use std::net::SocketAddr;
use std::sync::{Arc, RwLock};

use ain_evm_runtime::{Runtime, RUNTIME};

lazy_static::lazy_static! {
    // RPC clients cached globally based on address so that clients can be instantiated at will
    static ref CLIENTS: RwLock<HashMap<String, Client>> = RwLock::new(HashMap::new());
}

pub fn add_json_rpc_server(runtime: &Runtime, addr: &str) -> Result<(), Box<dyn Error>> {
    log::info!("Starting JSON RPC server at {}", addr);
    let addr = addr.parse::<SocketAddr>()?;
    let handle = runtime.rt_handle.clone();
    let server = runtime.rt_handle.block_on(
        HttpServerBuilder::default()
            .custom_tokio_runtime(handle)
            .build(addr),
    )?;
    let mut methods: Methods = Methods::new();
    methods.merge(BlockchainService::new(Arc::clone(&runtime.evm)).module()?)?;
    methods.merge(EthService::new(Arc::clone(&runtime.evm)).module()?)?;

    *runtime.jrpc_handle.lock().unwrap() = Some(server.start(methods)?);
    Ok(())
}

pub fn add_grpc_server(runtime: &Runtime, addr: &str) -> Result<(), Box<dyn Error>> {
    log::info!("Starting gRPC server at {}", addr);
    runtime.rt_handle.spawn(
        Server::builder()
            .add_service(BlockchainService::new(Arc::clone(&runtime.evm)).service())
            .add_service(EthService::new(Arc::clone(&runtime.evm)).service())
            .serve(addr.parse()?),
    );
    Ok(())
}

pub fn init_runtime() {
    log::info!("Starting gRPC and JSON RPC servers");
    LogBuilder::from_env(Env::default().default_filter_or(Level::Info.as_str())).init();
    let _ = &*RUNTIME;
}

pub fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<(), Box<dyn Error>> {
    add_json_rpc_server(&RUNTIME, json_addr)?;
    add_grpc_server(&RUNTIME, grpc_addr)?;
    Ok(())
}

pub fn stop_runtime() {
    log::info!("Stopping gRPC and JSON RPC servers");
    RUNTIME.stop();
}
