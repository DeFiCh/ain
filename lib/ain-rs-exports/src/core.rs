use crate::{ffi::CrossBoundaryResult, prelude::*};

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

pub fn ain_rs_init_network_json_rpc_service(result: &mut CrossBoundaryResult, addr: String) {
    match ain_grpc::init_network_json_rpc_service(addr) {
        Ok(()) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn ain_rs_init_network_grpc_service(result: &mut CrossBoundaryResult, addr: String) {
    match ain_grpc::init_network_grpc_service(addr) {
        Ok(()) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn ain_rs_init_network_subscriptions_service(result: &mut CrossBoundaryResult, addr: String) {
    match ain_grpc::init_network_subscriptions_service(addr) {
        Ok(()) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn ain_rs_stop_network_services(result: &mut CrossBoundaryResult) {
    match ain_grpc::stop_network_services() {
        Ok(()) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

pub fn ain_rs_wipe_evm_folder(result: &mut CrossBoundaryResult) {
    match ain_grpc::wipe_evm_folder() {
        Ok(()) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}
