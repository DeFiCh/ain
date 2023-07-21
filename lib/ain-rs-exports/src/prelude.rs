use crate::ffi;

pub fn cross_boundary_success(result: &mut ffi::CrossBoundaryResult) {
    result.ok = true;
    result.reason = Default::default();
}

pub fn cross_boundary_error<S: Into<String>>(result: &mut ffi::CrossBoundaryResult, message: S) {
    result.ok = false;
    result.reason = message.into();
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
