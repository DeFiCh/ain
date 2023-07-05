#[macro_use]
extern crate serde;
extern crate serde_json;

pub mod block;
pub mod call_request;
pub mod codegen;
mod filters;
mod impls;
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

struct LogTarget {}

// TODO:
// - Switch to u8 to avoid intermediate string conversions
// - Use a custom format to clear up timing.
impl std::io::Write for LogTarget {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        // let mut out = std::io::stdout().lock();
        // out.write(buf)?;
        let s = std::str::from_utf8(buf).or(Err(std::io::Error::new(
            std::io::ErrorKind::Other,
            "Invalid UTF-8 sequence",
        )))?;
        ain_cpp_imports::log_print(s);
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

pub fn preinit() {
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or(log::Level::Info.as_str()),
    )
    .target(env_logger::Target::Pipe(Box::new(LogTarget {})))
    .init();
    info!("init");
}

pub fn init_evm_runtime() {
    info!("init evm runtime");
    let _ = &*RUNTIME;
}

pub fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<(), Box<dyn Error>> {
    add_json_rpc_server(&RUNTIME, json_addr)?;
    add_grpc_server(&RUNTIME, grpc_addr)?;
    Ok(())
}

pub fn stop_evm_runtime() {
    info!("stop evm runtime");
    RUNTIME.stop();
}
