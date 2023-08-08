#[cxx::bridge]
pub mod ffi {
    pub struct AttributeDefaults {
        pub gas_target: u64,
        pub gas_limit: u64,
        pub finality_count: u64,
    }

    unsafe extern "C++" {
        include!("ffi/ffiexports.h");
        type AttributeDefaults;

        fn getChainId() -> u64;
        fn isMining() -> bool;
        fn publishEthTransaction(data: Vec<u8>) -> String;
        fn getAccounts() -> Vec<String>;
        fn getDatadir() -> String;
        fn getNetwork() -> String;
        fn getDifficulty(block_hash: [u8; 32]) -> u32;
        fn getChainWork(block_hash: [u8; 32]) -> [u8; 32];
        fn getPoolTransactions() -> Vec<String>;
        fn getNativeTxSize(data: Vec<u8>) -> u64;
        fn getMinRelayTxFee() -> u64;
        fn getEthPrivKey(key_id: [u8; 20]) -> [u8; 32];
        fn getStateInputJSON() -> String;
        fn getHighestBlock() -> i32;
        fn getCurrentHeight() -> i32;
        fn getAttributeDefaults() -> AttributeDefaults;
        fn CppLogPrintf(message: String);
    }
}
