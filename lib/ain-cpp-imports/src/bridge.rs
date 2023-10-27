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

    #[derive(Debug, Clone)]
    pub struct TransactionData {
        pub tx_type: u8,
        pub data: String,
        pub direction: u8,
    }

    unsafe extern "C++" {
        include!("ffi/ffiexports.h");
        type Attributes;
        type DST20Token;
        type TransactionData;

        fn getChainId() -> u64;
        fn isMining() -> bool;
        fn publishEvmTransaction(data: Vec<u8>) -> String;
        fn getAccounts() -> Vec<String>;
        fn getDatadir() -> String;
        fn getNetwork() -> String;
        fn getEvmMaxConnections() -> u32;
        fn getDifficulty(block_hash: [u8; 32]) -> u32;
        fn getChainWork(block_hash: [u8; 32]) -> [u8; 32];
        fn getPoolTransactions() -> Vec<TransactionData>;
        fn getNativeTxSize(data: Vec<u8>) -> u64;
        fn getMinRelayTxFee() -> u64;
        fn getEvmPrivKey(key: String) -> [u8; 32];
        fn getStateInputJSON() -> String;
        fn getHighestBlock() -> i32;
        fn getCurrentHeight() -> i32;
        fn getAttributeDefaults() -> Attributes;
        fn CppLogPrintf(message: String);
        fn getDST20Tokens(mnview_ptr: usize) -> Vec<DST20Token>;
        fn getClientVersion() -> String;
        fn getNumCores() -> i32;
        fn getCORSAllowedOrigin() -> String;
        fn getNumConnections() -> i32;
        fn getEvmCacheSizeLimit() -> usize;
    }
}
