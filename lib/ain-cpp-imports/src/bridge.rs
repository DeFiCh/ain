#[cxx::bridge]
pub mod ffi {
    #[derive(Debug, Clone, Serialize, Deserialize)]
    pub struct Attributes {
        pub block_gas_target_factor: u64,
        pub block_gas_limit: u64,
        pub finality_count: u64,
        pub rbf_fee_increment: u64,
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
        fn publishEthTransaction(data: Vec<u8>) -> String;
        fn getAccounts() -> Vec<String>;
        fn getDatadir() -> String;
        fn getNetwork() -> String;
        fn getEthMaxConnections() -> u32;
        fn getEthMaxResponseByteSize() -> u32;
        fn getDifficulty(block_hash: [u8; 32]) -> u32;
        fn getChainWork(block_hash: [u8; 32]) -> [u8; 32];
        fn getPoolTransactions() -> Vec<TransactionData>;
        fn getNativeTxSize(data: Vec<u8>) -> u64;
        fn getMinRelayTxFee() -> u64;
        fn getEthPrivKey(key: String) -> [u8; 32];
        fn getStateInputJSON() -> String;
        fn getEthSyncStatus() -> [i64; 2];
        fn getAttributeValues(mnview_ptr: usize) -> Attributes;
        fn CppLogPrintf(message: String);
        #[allow(clippy::ptr_arg)]
        fn getDST20Tokens(mnview_ptr: usize, tokens: &mut Vec<DST20Token>) -> bool;
        fn getClientVersion() -> String;
        fn getNumCores() -> i32;
        fn getCORSAllowedOrigin() -> String;
        fn getNumConnections() -> i32;
        fn getEccLruCacheCount() -> usize;
        fn getEvmValidationLruCacheCount() -> usize;
        fn isEthDebugRPCEnabled() -> bool;
        fn isEthDebugTraceRPCEnabled() -> bool;
        fn isOceanEnabled() -> bool;
    }
}
