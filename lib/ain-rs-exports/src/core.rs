use crate::ffi::CrossBoundaryResult;
use crate::prelude::*;

pub fn ain_rs_preinit(result: &mut CrossBoundaryResult) {
    ain_grpc::preinit();
    cross_boundary_success(result);
}

pub fn ain_rs_init_logging(result: &mut CrossBoundaryResult) {
    ain_grpc::init_logging();
    cross_boundary_success(result);
}

pub fn ain_rs_init_core_services(result: &mut CrossBoundaryResult) {
    ain_grpc::init_services();
    result.ok = true;
}

pub fn ain_rs_stop_core_services(result: &mut CrossBoundaryResult) {
    ain_grpc::stop_services();
    cross_boundary_success(result);
}

pub fn ain_rs_init_network_services(
    result: &mut CrossBoundaryResult,
    json_addr: &str,
    grpc_addr: &str,
) {
    match ain_grpc::init_network_services(json_addr, grpc_addr) {
        Ok(_) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn ain_rs_stop_network_services(result: &mut CrossBoundaryResult) {
    match ain_grpc::stop_network_services() {
        Ok(_) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn ain_rs_wipe_evm_folder(result: &mut CrossBoundaryResult) {
    match ain_grpc::wipe_evm_folder() {
        Ok(_) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}
