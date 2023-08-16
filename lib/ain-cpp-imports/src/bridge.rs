#[cxx::bridge]
pub mod ffi {
    #[derive(Debug, Clone, Serialize, Deserialize)]
    pub struct Attributes {
        pub block_gas_target: u64,
        pub block_gas_limit: u64,
        pub finality_count: u64,
    }

    #[derive(Debug, Clone)]
    pub struct DST20Token {
        pub id: u64,
        pub name: String,
        pub symbol: String,
    }

    unsafe extern "C++" {
        include!("ffi/ffiexports.h");
        type Attributes;
        type DST20Token;

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
        fn getAttributeDefaults() -> Attributes;
        fn CppLogPrintf(message: String);
        fn getDST20Tokens(mnview_ptr: usize) -> Vec<DST20Token>;
    }
}
