use std::error::Error;

#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("masternodes/evm_ffi.h");

        fn getChainId() -> u64;
    }
}

pub fn get_chain_id() -> Result<u64, Box<dyn Error>> {
    let chain_id = ffi::getChainId();

    return Ok(chain_id);
}
