use std::error::Error;

#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("ffi/ffiexports.h");

        fn getChainId() -> u64;
        fn isMining() -> bool;
        fn publishEthTransaction(data: Vec<u8>) -> bool;
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

pub fn publish_eth_transaction(data: Vec<u8>) -> Result<bool, Box<dyn Error>> {
    let publish = ffi::publishEthTransaction(data);
    Ok(publish)
}
