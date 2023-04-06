#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("masternodes/evm_ffi.h");

        fn getChainId() -> u64;
    }
}

pub fn get_chain_id() -> u64 {
    let chain_id = ffi::getChainId();

    return chain_id;
}
