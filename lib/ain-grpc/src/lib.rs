#[macro_use]
extern crate serde;
extern crate serde_json;

mod block;
mod call_request;
mod codegen;
mod impls;
mod receipt;
pub mod rpc;
pub use ain_evm::evm::EVMState;

use env_logger::{Builder as LogBuilder, Env, Target};
use jsonrpsee::core::server::rpc_module::Methods;
use jsonrpsee::http_server::HttpServerBuilder;
use log::Level;

use crate::rpc::{MetachainRPCModule, MetachainRPCServer};

use std::error::Error;
use std::net::SocketAddr;
use std::sync::Arc;
use std::os::raw::c_char;
use std::ffi::CStr;

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

pub unsafe fn init(_argc: i32, _argv: *const *const c_char) {
    LogBuilder::from_env(Env::default().default_filter_or(Level::Info.as_str()))
        .target(Target::Stdout)
        .init();
    log::info!("Initializing");

    let _args: Vec<&str> = (0.._argc)
        .map(|i| unsafe { CStr::from_ptr(*_argv.add(i as usize)).to_str().unwrap() }).collect();
}

pub fn init_evm_runtime() {
    log::info!("Initializing evm runtime");
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
