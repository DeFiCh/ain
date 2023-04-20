#[macro_use]
extern crate serde;
extern crate serde_json;

mod block;
mod call_request;
mod codegen;
mod impls;
pub mod rpc;

use env_logger::{Builder as LogBuilder, Env, Target};
use jsonrpsee::core::server::rpc_module::Methods;
use jsonrpsee::http_server::HttpServerBuilder;
use log::Level;

use crate::rpc::{MetachainRPCModule, MetachainRPCServer};

use std::error::Error;
use std::net::SocketAddr;
use std::sync::Arc;

use ain_evm::runtime::{Runtime, RUNTIME};

#[cfg(test)]
mod tests;

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
    methods.merge(MetachainRPCModule::new(Arc::clone(&runtime.handlers)).into_rpc())?;

    *runtime.jrpc_handle.lock().unwrap() = Some(server.start(methods)?);
    Ok(())
}

pub fn add_grpc_server(_runtime: &Runtime, _addr: &str) -> Result<(), Box<dyn Error>> {
    // log::info!("Starting gRPC server at {}", addr);
    // Commented out for now as nothing to serve
    // runtime
    //     .rt_handle
    // .spawn(Server::builder().serve(addr.parse()?));

    Ok(())
}

pub fn init_runtime() {
    log::info!("Starting gRPC and JSON RPC servers");
    LogBuilder::from_env(Env::default().default_filter_or(Level::Info.as_str()))
        .target(Target::Stdout)
        .init();
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
