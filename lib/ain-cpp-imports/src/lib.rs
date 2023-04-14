use std::error::Error;

#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("masternodes/ffi_exports.h");

        fn getChainId() -> u64;
        fn isMining() -> bool;
    }
}

pub fn get_chain_id() -> Result<u64, Box<dyn Error>> {
    let chain_id = ffi::getChainId();
    Ok(chain_id)
}

pub fn is_mining() -> Result<bool, Box<dyn Error>> {
    let is_mining = ffi::isMining();
    Ok(is_mining)
}
