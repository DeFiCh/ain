use bigint::{Address, Gas, H256, M256, U256};
pub use common::{c_address, c_gas, c_h256, c_u256};
use libc::{c_char, c_longlong, c_uchar, c_uint, c_ulonglong};
use std::ffi::{CStr, CString};

#[no_mangle]
pub extern "C" fn u256_from_str(s: *const c_char) -> c_u256 {
    unsafe {
        let u256_as_str = CStr::from_ptr(s).to_str().unwrap();
        c_u256::from(U256::from(u256_as_str))
    }
}

#[no_mangle]
pub extern "C" fn h256_from_str(s: *const c_char) -> c_h256 {
    unsafe {
        let h256_as_str = CStr::from_ptr(s).to_str().unwrap();
        c_h256::from(H256::from(h256_as_str))
    }
}

#[no_mangle]
pub extern "C" fn address_from_str(s: *const c_char) -> c_address {
    unsafe {
        let address_as_str = CStr::from_ptr(s).to_str().unwrap();
        c_address::from(Address::from(address_as_str))
    }
}

#[no_mangle]
pub extern "C" fn gas_from_u64(s: c_ulonglong) -> c_gas {
    c_gas::from(Gas::from(s))
}

#[no_mangle]
pub extern "C" fn print_gas(g: c_gas) {
    let gas: Gas = g.into();
    println!("{:?}", gas);
}
