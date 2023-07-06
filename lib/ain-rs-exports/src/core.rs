use crate::ffi::CrossBoundaryResult;

pub fn ain_rs_preinit(result: &mut CrossBoundaryResult) {
    ain_grpc::preinit();
    result.ok = true;
}

pub fn ain_rs_init_logging(result: &mut CrossBoundaryResult) {
    ain_grpc::init_logging();
    result.ok = true;
}

pub fn ain_rs_init_core_services(result: &mut CrossBoundaryResult) {
    ain_grpc::init_services();
    result.ok = true;
}

pub fn ain_rs_stop_core_services(result: &mut CrossBoundaryResult) {
    ain_grpc::stop_services();
    result.ok = true;
}

pub fn ain_rs_init_network_services(
    result: &mut CrossBoundaryResult,
    json_addr: &str,
    grpc_addr: &str,
) {
    match ain_grpc::init_network_services(json_addr, grpc_addr) {
        Ok(()) => {
            result.ok = true;
        }
        Err(e) => {
            result.ok = false;
            result.reason = e.to_string();
        }
    }
}

pub fn ain_rs_stop_network_services(result: &mut CrossBoundaryResult) {
    ain_grpc::stop_network_services();
    result.ok = true;
}
