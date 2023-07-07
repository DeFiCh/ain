#[macro_use]
extern crate serde;
extern crate serde_json;

pub mod block;
pub mod call_request;
pub mod codegen;
mod filters;
mod impls;
pub mod logging;
mod receipt;
pub mod rpc;
mod sync;
mod transaction;
mod transaction_log;
mod transaction_request;
mod utils;

#[cfg(test)]
mod tests;

use jsonrpsee::core::server::rpc_module::Methods;
use jsonrpsee::http_server::HttpServerBuilder;

#[allow(unused)]
use log::{debug, info};
use logging::CppLogTarget;

use crate::rpc::{
    debug::{MetachainDebugRPCModule, MetachainDebugRPCServer},
    eth::{MetachainRPCModule, MetachainRPCServer},
    net::{MetachainNetRPCModule, MetachainNetRPCServer},
};

use std::net::SocketAddr;
use std::sync::Arc;

use ain_evm::services::{Services, SERVICES};
use anyhow::Result;

// TODO: Ideally most of the below and SERVICES needs to go into its own core crate now, 
// and this crate be dedicated to network services. 
// Note: This cannot just move to rs-exports, since rs-exports cannot cannot have reverse
// deps that depend on it. 

pub fn preinit() {}

pub fn init_logging() {
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or(log::Level::Info.as_str()),
    )
    .format(logging::cpp_log_target_format)
    .target(env_logger::Target::Pipe(Box::new(CppLogTarget::new(false))))
    .init();
    info!("Init rs logging");
}

pub fn init_services() {
    info!("Init rs services");
    let _ = &*SERVICES;
}

pub fn init_network_services(json_addr: &str, grpc_addr: &str) -> Result<()> {
    init_network_json_rpc_service(&SERVICES, json_addr)?;
    init_network_grpc_service(&SERVICES, grpc_addr)?;
    Ok(())
}

pub fn init_network_json_rpc_service(runtime: &Services, addr: &str) -> Result<()> {
    info!("Starting JSON RPC server at {}", addr);
    let addr = addr.parse::<SocketAddr>()?;
    let handle = runtime.tokio_runtime.clone();
    let server = runtime.tokio_runtime.block_on(
        HttpServerBuilder::default()
            .custom_tokio_runtime(handle)
            .build(addr),
    )?;
    let mut methods: Methods = Methods::new();
    methods.merge(MetachainRPCModule::new(Arc::clone(&runtime.evm)).into_rpc())?;
    methods.merge(MetachainDebugRPCModule::new(Arc::clone(&runtime.evm)).into_rpc())?;
    methods.merge(MetachainNetRPCModule::new(Arc::clone(&runtime.evm)).into_rpc())?;

    *runtime.json_rpc.lock().unwrap() = Some(server.start(methods)?);
    Ok(())
}

pub fn init_network_grpc_service(_runtime: &Services, _addr: &str) -> Result<()> {
    // log::info!("Starting gRPC server at {}", addr);
    // Commented out for now as nothing to serve
    // runtime
    //     .rt_handle
    // .spawn(Server::builder().serve(addr.parse()?));
    Ok(())
}

pub fn stop_network_services() -> Result<()> {
    info!("Shutdown rs network services");
    SERVICES.stop_network()
}

pub fn stop_services() {
    info!("Shutdown rs services");
    SERVICES.stop();
}
