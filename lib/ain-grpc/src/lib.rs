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

use std::error::Error;
use std::net::SocketAddr;
use std::sync::Arc;

use ain_evm::runtime::{Runtime, RUNTIME};

#[cfg(test)]
mod tests;

pub fn add_json_rpc_server(runtime: &Runtime, addr: &str) -> Result<(), Box<dyn Error>> {
    info!("Starting JSON RPC server at {}", addr);
    let addr = addr.parse::<SocketAddr>()?;
    let handle = runtime.rt_handle.clone();
    let server = runtime.rt_handle.block_on(
        HttpServerBuilder::default()
            .custom_tokio_runtime(handle)
            .build(addr),
    )?;
    let mut methods: Methods = Methods::new();
    methods.merge(MetachainRPCModule::new(Arc::clone(&runtime.handlers)).into_rpc())?;
    methods.merge(MetachainDebugRPCModule::new(Arc::clone(&runtime.handlers)).into_rpc())?;
    methods.merge(MetachainNetRPCModule::new(Arc::clone(&runtime.handlers)).into_rpc())?;

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

pub fn preinit() {
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or(log::Level::Info.as_str()),
    )
    .format(logging::cpp_log_target_format)
    .target(env_logger::Target::Pipe(Box::new(CppLogTarget::new(false))))
    .init();
    info!("init");
}

pub fn init_evm_runtime() {
    info!("init evm runtime");
    let _ = &*RUNTIME;
}

pub fn start_evm_servers(json_addr: &str, grpc_addr: &str) -> Result<(), Box<dyn Error>> {
    add_json_rpc_server(&RUNTIME, json_addr)?;
    add_grpc_server(&RUNTIME, grpc_addr)?;
    Ok(())
}

pub fn stop_evm_runtime() {
    info!("stop evm runtime");
    RUNTIME.stop();
}
