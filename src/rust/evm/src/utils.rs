use bigint::{Address, Gas, H256, M256, U256};
pub use common::{c_address, c_gas, c_h256, c_u256};
use libc::{c_char, c_longlong, c_uchar, c_uint};
use std::ffi::{CStr, CString};

#[no_mangle]
pub extern "C" fn u256_from_str(s: *const c_char) -> c_u256 {
    unsafe {
        let slice = CStr::from_ptr(s).to_str().unwrap();
        c_u256::from(U256::from(slice))
    }
}

#[no_mangle]
pub extern "C" fn h256_from_str(s: *const c_char) -> c_h256 {
    unsafe {
        let slice = CStr::from_ptr(s).to_str().unwrap();
        c_h256::from(H256::from(slice))
    }
}

#[no_mangle]
pub extern "C" fn address_from_str(s: *const c_char) -> c_address {
    unsafe {
        let slice = CStr::from_ptr(s).to_str().unwrap();
        c_address::from(Address::from(slice))
    }
}

#[no_mangle]
pub extern "C" fn print_gas(g: c_gas) {
    let gas: Gas = g.into();
    println!("{:?}", gas);
}
