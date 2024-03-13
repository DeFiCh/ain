use crate::{ffi, prelude::*};
use ain_macros::ffi_fallible;
use anyhow::Result;

#[ffi_fallible]
pub fn ain_rs_preinit() -> Result<()> {
    ain_grpc::preinit();
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_init_logging() -> Result<()> {
    ain_grpc::init_logging();
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_init_core_services() -> Result<()> {
    ain_grpc::init_services();
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_stop_core_services() -> Result<()> {
    ain_grpc::stop_services()?;
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_init_network_json_rpc_service(addr: String) -> Result<()> {
    ain_grpc::init_network_json_rpc_service(addr)?;
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_init_network_grpc_service(addr: String) -> Result<()> {
    ain_grpc::init_network_grpc_service(addr)?;
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_init_network_subscriptions_service(addr: String) -> Result<()> {
    ain_grpc::init_network_subscriptions_service(addr)?;
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_stop_network_services() -> Result<()> {
    ain_grpc::stop_network_services()?;
    Ok(())
}

#[ffi_fallible]
pub fn ain_rs_wipe_evm_folder() -> Result<()> {
    ain_grpc::wipe_evm_folder()?;
    Ok(())
}
