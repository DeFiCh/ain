use crate::ffi;
use log::debug;

pub fn cross_boundary_success(result: &mut ffi::CrossBoundaryResult) {
    result.ok = true;
    result.reason = String::default();
}

pub fn cross_boundary_error<S: Into<String>>(result: &mut ffi::CrossBoundaryResult, message: S) {
    result.ok = false;
    result.reason = message.into();
    debug!("[cross_boundary_error] reason: {:?}", result.reason);
}

pub fn cross_boundary_error_return<T: Default, S: Into<String>>(
    result: &mut ffi::CrossBoundaryResult,
    message: S,
) -> T {
    cross_boundary_error(result, message);
    Default::default()
}

pub fn cross_boundary_success_return<T>(result: &mut ffi::CrossBoundaryResult, item: T) -> T {
    cross_boundary_success(result);
    item
}

pub fn try_cross_boundary_return<T: Default, E: ToString>(
    result: &mut ffi::CrossBoundaryResult,
    item: Result<T, E>,
) -> T {
    match item {
        Err(e) => {
            cross_boundary_error(result, e.to_string());
            Default::default()
        }
        Ok(v) => {
            cross_boundary_success(result);
            v
        }
    }
}
