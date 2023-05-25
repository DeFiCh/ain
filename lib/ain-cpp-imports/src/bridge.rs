#[cxx::bridge]
pub mod ffi {
    pub struct SyncStatusMetadata {
        pub syncing: bool,
        pub starting_block: u64,
        pub current_block: u64,
        pub highest_block: u64,
    }

    unsafe extern "C++" {
        include!("ffi/ffiexports.h");
        type SyncStatusMetadata;

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
        fn getSyncStatus() -> SyncStatusMetadata;
    }
}
