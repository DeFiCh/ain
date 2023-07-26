mod core;
mod evm;
mod prelude;

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
        pub total_burnt_fees: u64,
        pub total_priority_fees: u64,
    }

    #[derive(Default)]
    pub struct PreValidateTxCompletion {
        pub nonce: u64,
        pub sender: [u8; 20],
        pub prepay_fee: u64,
    }

    #[derive(Default)]
    pub struct ValidateTxCompletion {
        pub nonce: u64,
        pub sender: [u8; 20],
        pub prepay_fee: u64,
        pub gas_used: u64,
    }

    extern "Rust" {
        // In-fallible functions
        //
        // If they are fallible, it's a TODO to changed and move later
        // so errors are propogated up properly.
        fn evm_get_balance(address: [u8; 20]) -> u64;
        fn evm_get_next_valid_nonce_in_queue(queue_id: u64, address: [u8; 20]) -> u64;
        fn evm_remove_txs_by_sender(queue_id: u64, address: [u8; 20]);
        fn evm_add_balance(
            queue_id: u64,
            address: &str,
            amount: [u8; 32],
            native_tx_hash: [u8; 32],
        );
        fn evm_sub_balance(
            queue_id: u64,
            address: &str,
            amount: [u8; 32],
            native_tx_hash: [u8; 32],
        ) -> bool;
        fn evm_get_queue_id() -> u64;
        fn evm_discard_context(queue_id: u64);
        fn evm_disconnect_latest_block();

        // Failible functions
        // Has to take CrossBoundaryResult as first param
        // Has to start with try_ / evm_try

        fn evm_try_prevalidate_raw_tx(
            result: &mut CrossBoundaryResult,
            tx: &str,
        ) -> PreValidateTxCompletion;
        fn evm_try_validate_raw_tx(
            result: &mut CrossBoundaryResult,
            tx: &str,
            queue_id: u64,
        ) -> ValidateTxCompletion;
        fn evm_try_queue_tx(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            raw_tx: &str,
            hash: [u8; 32],
            gas_used: u64,
        );
        fn evm_try_finalize(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            update_state: bool,
            difficulty: u32,
            miner_address: [u8; 20],
            timestamp: u64,
        ) -> FinalizeBlockCompletion;
        fn evm_try_create_and_sign_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransactionContext,
        ) -> Vec<u8>;

        fn evm_try_get_block_hash_by_number(
            result: &mut CrossBoundaryResult,
            height: u64,
        ) -> [u8; 32];
        fn evm_try_get_block_number_by_hash(
            result: &mut CrossBoundaryResult,
            hash: [u8; 32],
        ) -> u64;
    }
}
