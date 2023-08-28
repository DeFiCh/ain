mod core;
mod evm;
mod prelude;

use crate::core::*;
use crate::evm::*;

#[cxx::bridge]
pub mod ffi {
    // ========== Block ==========
    #[derive(Default)]
    pub struct EVMBlockHeader {
        pub parent_hash: String,
        pub beneficiary: String,
        pub state_root: String,
        pub receipts_root: String,
        pub number: u64,
        pub gas_limit: u64,
        pub gas_used: u64,
        pub timestamp: u64,
        pub extra_data: Vec<u8>,
        pub mix_hash: String,
        pub nonce: u64,
        pub base_fee: u64,
    }

    // ========== Transaction ==========
    #[derive(Default)]
    pub struct EVMTransaction {
        // EIP-2718 transaction type: legacy - 0x0, EIP2930 - 0x1, EIP1559 - 0x2
        pub tx_type: u8,
        pub hash: String,
        pub sender: String,
        pub nonce: u64,
        pub gas_price: u64,
        pub gas_limit: u64,
        pub max_fee_per_gas: u64,
        pub max_priority_fee_per_gas: u64,
        pub create_tx: bool,
        pub to: String,
        pub value: u64,
        pub data: Vec<u8>,
    }

    // =========  Core ==========
    pub struct CrossBoundaryResult {
        pub ok: bool,
        pub reason: String,
    }

    extern "Rust" {
        fn ain_rs_preinit(result: &mut CrossBoundaryResult);
        fn ain_rs_init_logging(result: &mut CrossBoundaryResult);
        fn ain_rs_init_core_services(result: &mut CrossBoundaryResult);
        fn ain_rs_wipe_evm_folder(result: &mut CrossBoundaryResult);
        fn ain_rs_stop_core_services(result: &mut CrossBoundaryResult);
        fn ain_rs_init_network_services(
            result: &mut CrossBoundaryResult,
            json_addr: &str,
            grpc_addr: &str,
        );
        fn ain_rs_stop_network_services(result: &mut CrossBoundaryResult);
    }

    // ========== EVM ==========

    pub struct CreateTransactionContext<'a> {
        pub chain_id: u64,
        pub nonce: u64,
        pub gas_price: u64,
        pub gas_limit: u64,
        pub to: &'a str,
        pub value: u64,
        pub input: Vec<u8>,
        pub priv_key: [u8; 32],
    }

    #[derive(Default)]
    pub struct FinalizeBlockCompletion {
        pub block_hash: String,
        pub failed_transactions: Vec<String>,
        pub total_burnt_fees: u64,
        pub total_priority_fees: u64,
        pub block_number: u64,
    }

    #[derive(Default)]
    pub struct ValidateTxCompletion {
        pub nonce: u64,
        pub sender: String,
        pub tx_hash: String,
        pub prepay_fee: u64,
        pub gas_used: u64,
    }

    extern "Rust" {
        // In-fallible functions
        //
        // If they are fallible, it's a TODO to changed and move later
        // so errors are propogated up properly.
        fn evm_try_get_balance(result: &mut CrossBoundaryResult, address: &str) -> u64;
        fn evm_unsafe_try_create_queue(result: &mut CrossBoundaryResult) -> u64;
        fn evm_unsafe_try_remove_queue(result: &mut CrossBoundaryResult, queue_id: u64);
        fn evm_try_disconnect_latest_block(result: &mut CrossBoundaryResult);

        // Failible functions
        // Has to take CrossBoundaryResult as first param
        // Has to start with try_ / evm_try
        fn evm_unsafe_try_get_next_valid_nonce_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            address: &str,
        ) -> u64;
        fn evm_unsafe_try_remove_txs_by_sender_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            address: &str,
        );
        fn evm_unsafe_try_add_balance_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            address: &str,
            amount: u64,
            native_hash: &str,
        );
        fn evm_unsafe_try_sub_balance_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            address: &str,
            amount: u64,
            native_hash: &str,
        ) -> bool;
        fn evm_unsafe_try_prevalidate_raw_tx(
            result: &mut CrossBoundaryResult,
            tx: &str,
        ) -> ValidateTxCompletion;
        fn evm_unsafe_try_validate_raw_tx_in_q(
            result: &mut CrossBoundaryResult,
            tx: &str,
            queue_id: u64,
        ) -> ValidateTxCompletion;
        fn evm_unsafe_try_push_tx_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            raw_tx: &str,
            native_hash: &str,
            gas_used: u64,
        );
        fn evm_unsafe_try_construct_block_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            difficulty: u32,
            miner_address: &str,
            timestamp: u64,
            dvm_block_number: u64,
            mnview_ptr: usize,
        ) -> FinalizeBlockCompletion;
        fn evm_unsafe_try_commit_queue(result: &mut CrossBoundaryResult, queue_id: u64);
        fn evm_try_set_attribute(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            attribute_type: u32,
            value: u64,
        ) -> bool;
        fn evm_try_create_and_sign_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransactionContext,
        ) -> Vec<u8>;
        fn evm_try_create_and_sign_transfer_domain_dst20_tx(
            result: &mut CrossBoundaryResult,
            to: &str,
            amount: u64,
            nonce: u64,
            token_id: u64,
            chain_id: u64,
            priv_key: [u8; 32],
        ) -> Vec<u8>;
        fn evm_try_get_block_hash_by_number(
            result: &mut CrossBoundaryResult,
            height: u64,
        ) -> String;
        fn evm_try_get_block_number_by_hash(result: &mut CrossBoundaryResult, hash: &str) -> u64;
        fn evm_try_get_block_header_by_hash(
            result: &mut CrossBoundaryResult,
            hash: &str,
        ) -> EVMBlockHeader;
        fn evm_try_get_block_count(result: &mut CrossBoundaryResult) -> u64;
        fn evm_try_get_tx_by_hash(
            result: &mut CrossBoundaryResult,
            tx_hash: &str,
        ) -> EVMTransaction;

        fn evm_try_create_dst20(
            result: &mut CrossBoundaryResult,
            context: u64,
            native_hash: &str,
            name: &str,
            symbol: &str,
            token_id: u64,
        );
        fn evm_try_bridge_dst20(
            result: &mut CrossBoundaryResult,
            context: u64,
            address: &str,
            amount: u64,
            native_hash: &str,
            token_id: u64,
            out: bool,
        );
        fn evm_try_is_dst20_deployed_or_queued(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            name: &str,
            symbol: &str,
            token_id: u64,
        ) -> bool;

        fn evm_unsafe_try_get_target_block_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
        ) -> u64;
    }
}
