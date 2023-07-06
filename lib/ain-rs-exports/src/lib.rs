mod core;
mod evm;

use crate::core::*;
use crate::evm::*;

#[cxx::bridge]
pub mod ffi {
    // =========  Core ==========
    pub struct CrossBoundaryResult {
        pub ok: bool,
        pub reason: String,
    }

    extern "Rust" {
        fn ain_rs_preinit(result: &mut CrossBoundaryResult);
        fn ain_rs_init_logging(result: &mut CrossBoundaryResult);
        fn ain_rs_init_core_services(result: &mut CrossBoundaryResult);
        fn ain_rs_stop_core_services(result: &mut CrossBoundaryResult);
        fn ain_rs_init_network_services(
            result: &mut CrossBoundaryResult,
            json_addr: &str,
            grpc_addr: &str,
        );
        fn ain_rs_stop_network_services(result: &mut CrossBoundaryResult);
    }

    // ========== EVM ==========

    pub struct CreateTransactionContext {
        pub chain_id: u64,
        pub nonce: [u8; 32],
        pub gas_price: [u8; 32],
        pub gas_limit: [u8; 32],
        pub to: [u8; 20],
        pub value: [u8; 32],
        pub input: Vec<u8>,
        pub priv_key: [u8; 32],
    }

    #[derive(Default)]
    pub struct FinalizeBlockCompletion {
        pub block_hash: [u8; 32],
        pub failed_transactions: Vec<String>,
        pub miner_fee: u64,
    }

    #[derive(Default)]
    pub struct ValidateTxCompletion {
        pub nonce: u64,
        pub sender: [u8; 20],
        pub used_gas: u64,
    }

    extern "Rust" {
        fn evm_get_balance(address: [u8; 20]) -> u64;
        fn evm_get_next_valid_nonce_in_context(context: u64, address: [u8; 20]) -> u64;
        fn evm_remove_txs_by_sender(context: u64, address: [u8; 20]);
        fn evm_add_balance(context: u64, address: &str, amount: [u8; 32], native_tx_hash: [u8; 32]);
        fn evm_sub_balance(
            context: u64,
            address: &str,
            amount: [u8; 32],
            native_tx_hash: [u8; 32],
        ) -> bool;
        fn evm_try_prevalidate_raw_tx(
            result: &mut CrossBoundaryResult,
            tx: &str,
            with_gas_usage: bool,
        ) -> ValidateTxCompletion;
        fn evm_get_context() -> u64;
        fn evm_discard_context(context: u64);
        fn evm_try_queue_tx(
            result: &mut CrossBoundaryResult,
            context: u64,
            raw_tx: &str,
            native_tx_hash: [u8; 32],
        );
        fn evm_try_finalize(
            result: &mut CrossBoundaryResult,
            context: u64,
            update_state: bool,
            difficulty: u32,
            miner_address: [u8; 20],
            timestamp: u64,
        ) -> FinalizeBlockCompletion;
        fn evm_create_and_sign_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransactionContext,
        ) -> Vec<u8>;

        fn evm_disconnect_latest_block();
    }
}
