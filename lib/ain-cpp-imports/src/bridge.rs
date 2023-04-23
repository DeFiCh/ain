#[cxx::bridge]
pub mod ffi {
    unsafe extern "C++" {
        include!("ffi/ffiexports.h");

        fn getChainId() -> u64;
        fn isMining() -> bool;
        fn publishEthTransaction(data: Vec<u8>) -> bool;
        fn getAccounts() -> Vec<String>;
        fn getDatadir() -> String;
        fn getDifficulty(_block_hash: [u8; 32]) -> u32;
        fn getChainWork(_block_hash: [u8; 32]) -> [u8; 32];
        fn getNativeTxSize(data: Vec<u8>) -> u64;
        fn getMinRelayTxFee() -> u64;
    }
}
