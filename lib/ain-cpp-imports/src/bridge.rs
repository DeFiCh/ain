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
    pub struct PoolPairCreationHeight {
        pub id: u32,
        pub id_token_a: u32,
        pub id_token_b: u32,
        pub creation_height: u32,
    }

    #[derive(Debug, Clone)]
    pub struct DSTToken {
        pub id: u32,
        pub name: String,
        pub symbol: String,
        pub symbol_key: String,
        pub decimal: u8,
        pub is_dat: bool,
        pub is_lps: bool,
        pub tradable: bool,
        pub mintable: bool,
        pub finalize: bool,
        pub is_loan_token: bool,
        pub minted: i64,
        pub limit: i64,
        pub creation_tx: String,
        pub creation_height: i32,
        pub destruction_tx: String,
        pub destruction_height: i32,
        pub collateral_address: String,
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
        pub entry_time: i64,
    }

    #[derive(Debug, Clone)]
    pub enum SystemTxType {
        EVMTx,
        TransferDomainIn,
        TransferDomainOut,
        DST20BridgeIn,
        DST20BridgeOut,
        DeployContract,
        UpdateContractName,
    }

    #[derive(Debug, Clone)]
    pub struct SystemTxData {
        pub tx_type: SystemTxType,
        pub token: DST20Token,
    }

    #[derive(Debug, Clone)]
    pub struct TokenAmount {
        pub id: u32,
        pub amount: u64,
    }

    unsafe extern "C++" {
        include!("ffi/ffiexports.h");
        type Attributes;
        type PoolPairCreationHeight;
        type DSTToken;
        type DST20Token;
        type TransactionData;
        type SystemTxType;
        type SystemTxData;
        type TokenAmount;

        fn getChainId() -> u64;
        fn getRPCPort() -> i32;
        fn getRPCAuth() -> String;
        fn isMining() -> bool;
        fn publishEthTransaction(data: Vec<u8>) -> String;
        fn getAccounts() -> Vec<String>;
        fn getDatadir() -> String;
        fn getNetwork() -> String;
        fn getEthMaxConnections() -> u32;
        fn printEVMPortUsage(port_type: u8, port_number: u16);
        fn getEthMaxResponseByteSize() -> u32;
        fn getEthTracingMaxMemoryUsageBytes() -> u32;
        fn getSuggestedPriorityFeePercentile() -> i64;
        fn getEstimateGasErrorRatio() -> u64;
        fn getDifficulty(block_hash: [u8; 32]) -> u32;
        fn getChainWork(block_hash: [u8; 32]) -> [u8; 32];
        fn getPoolTransactions() -> Vec<TransactionData>;
        fn getNativeTxSize(data: Vec<u8>) -> u64;
        fn getMinRelayTxFee() -> u64;
        fn getEthPrivKey(key: [u8; 20]) -> [u8; 32];
        fn getStateInputJSON() -> String;
        fn getEthSyncStatus() -> [i64; 2];
        fn getAttributeValues(mnview_ptr: usize) -> Attributes;
        fn CppLogPrintf(message: String);
        fn getPoolPairs() -> Vec<PoolPairCreationHeight>;
        fn getDSTToken(id: String) -> UniquePtr<DSTToken>;
        #[allow(clippy::ptr_arg)]
        fn getDST20Tokens(mnview_ptr: usize, tokens: &mut Vec<DST20Token>) -> bool;
        fn getClientVersion() -> String;
        fn getNumCores() -> i32;
        fn getCORSAllowedOrigin() -> String;
        fn getNumConnections() -> i32;
        fn getEccLruCacheCount() -> usize;
        fn getEvmValidationLruCacheCount() -> usize;
        fn getEvmNotificationChannelBufferSize() -> usize;
        fn isEthDebugRPCEnabled() -> bool;
        fn isEthDebugTraceRPCEnabled() -> bool;
        fn getEVMSystemTxsFromBlock(block_hash: [u8; 32]) -> Vec<SystemTxData>;
        fn getDF23Height() -> u64;
        fn getDF24Height() -> u64;
        fn migrateTokensFromEVM(
            mnview_ptr: usize,
            old_amount: TokenAmount,
            new_amount: &mut TokenAmount,
        ) -> bool;
        fn isSkippedTx(tx_hash: [u8; 32]) -> bool;
    }
}
