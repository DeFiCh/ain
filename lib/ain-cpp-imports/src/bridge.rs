#[cxx::bridge]
pub mod ffi {
    unsafe extern "C++" {
        include!("ffi/ffiexports.h");

        fn getChainId() -> u64;
        fn isMining() -> bool;
        fn publishEthTransaction(data: Vec<u8>) -> bool;
        fn getAccounts() -> Vec<String>;
        fn getDatadir() -> String;
    }
}