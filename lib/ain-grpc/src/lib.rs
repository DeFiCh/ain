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

use std::{
    net::SocketAddr,
    path::PathBuf,
    sync::{atomic::Ordering, Arc},
};

use ain_evm::services::{Services, IS_SERVICES_INIT_CALL, SERVICES};
use anyhow::{format_err, Result};
use jsonrpsee::core::server::rpc_module::Methods;
use jsonrpsee_server::ServerBuilder as HttpServerBuilder;
use log::info;
use logging::CppLogTarget;

use crate::rpc::{
    debug::{MetachainDebugRPCModule, MetachainDebugRPCServer},
    eth::{MetachainRPCModule, MetachainRPCServer},
    net::{MetachainNetRPCModule, MetachainNetRPCServer},
    web3::{MetachainWeb3RPCModule, MetachainWeb3RPCServer},
};

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
    let max_connections = ain_cpp_imports::get_max_connections();

    let handle = runtime.tokio_runtime.clone();
    let server = runtime.tokio_runtime.block_on(
        HttpServerBuilder::default()
            .max_connections(max_connections)
            .custom_tokio_runtime(handle)
            .build(addr),
    )?;
    let mut methods: Methods = Methods::new();
    methods.merge(MetachainRPCModule::new(Arc::clone(&runtime.evm)).into_rpc())?;
    methods.merge(MetachainDebugRPCModule::new(Arc::clone(&runtime.evm)).into_rpc())?;
    methods.merge(MetachainNetRPCModule::new(Arc::clone(&runtime.evm)).into_rpc())?;
    methods.merge(MetachainWeb3RPCModule::new(Arc::clone(&runtime.evm)).into_rpc())?;

    *runtime.json_rpc.lock() = Some(server.start(methods)?);
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

fn is_services_init_called() -> bool {
    IS_SERVICES_INIT_CALL.load(Ordering::SeqCst)
}

pub fn stop_network_services() -> Result<()> {
    if is_services_init_called() {
        info!("Shutdown rs network services");
        SERVICES.stop_network()?;
    }
    Ok(())
}

pub fn stop_services() {
    if is_services_init_called() {
        info!("Shutdown rs services");
        SERVICES.stop();
    }
}

pub fn wipe_evm_folder() -> Result<()> {
    let datadir = ain_cpp_imports::get_datadir();
    let path = PathBuf::from(datadir).join("evm");
    info!("Wiping rs storage in {}", path.display());
    if path.exists() {
        std::fs::remove_dir_all(&path).map_err(|e| format_err!("Error wiping evm dir: {e}"))?;
    }
    Ok(())
}
