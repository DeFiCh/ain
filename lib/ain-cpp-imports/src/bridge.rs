#[cxx::bridge]
pub mod ffi {
    unsafe extern "C++" {
        include!("ffi/ffiexports.h");

        fn getChainId() -> u64;
        fn isMining() -> bool;
        fn publishEthTransaction(data: Vec<u8>) -> String;
        fn getAccounts() -> Vec<String>;
        fn getDatadir() -> String;
        fn getNetwork() -> String;
        fn getDifficulty(_block_hash: [u8; 32]) -> u32;
        fn getChainWork(_block_hash: [u8; 32]) -> [u8; 32];
        fn getPoolTransactions() -> Vec<String>;
        fn getNativeTxSize(data: Vec<u8>) -> u64;
        fn getMinRelayTxFee() -> u64;
        fn getEthPrivKey(key_id: [u8; 20]) -> [u8; 32];
        fn getStateInputJSON() -> String;
        fn isPostChangiIntermediateFork() -> bool;
    }
}
