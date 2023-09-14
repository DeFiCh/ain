mod core;
mod evm;
mod prelude;

use crate::{core::*, evm::*};

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

    #[derive(Default)]
    pub struct TxSenderInfo {
        address: String,
        nonce: u64,
    }

    // ========== Governance Variable ==========
    #[derive(Default)]
    pub struct GovVarKeyDataStructure {
        pub category: u8,
        pub category_id: u32,
        pub key: u32,
        pub key_id: u32,
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

    pub struct CreateTransferDomainContext {
        pub from: String,
        pub to: String,
        pub native_address: String,
        pub direction: bool,
        pub value: u64,
        pub token_id: u32,
        pub chain_id: u64,
        pub priv_key: [u8; 32],
        pub queue_id: u64,
    }

    #[derive(Default)]
    pub struct FinalizeBlockCompletion {
        pub block_hash: String,
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
    }

    #[derive(Default)]
    pub struct ValidateTxMiner {
        pub nonce: u64,
        pub sender: String,
        pub tx_hash: String,
        pub prepay_fee: u64,
        pub higher_nonce: bool,
        pub lower_nonce: bool,
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
        fn evm_unsafe_try_remove_txs_above_hash_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            target_hash: String,
        ) -> Vec<String>;
        fn evm_unsafe_try_add_balance_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            raw_tx: &str,
            native_hash: &str,
            pre_validate: bool,
        );
        fn evm_unsafe_try_sub_balance_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            raw_tx: &str,
            native_hash: &str,
            pre_validate: bool,
        ) -> bool;
        fn evm_unsafe_try_validate_raw_tx_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            raw_tx: &str,
            pre_validate: bool,
        ) -> ValidateTxMiner;
        fn evm_unsafe_try_push_tx_in_q(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            raw_tx: &str,
            native_hash: &str,
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
        fn evm_try_handle_attribute_apply(
            result: &mut CrossBoundaryResult,
            queue_id: u64,
            attribute_type: GovVarKeyDataStructure,
            value: Vec<u8>,
        ) -> bool;
        fn evm_try_create_and_sign_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransactionContext,
        ) -> Vec<u8>;
        fn evm_try_create_and_sign_transfer_domain_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransferDomainContext,
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
        fn evm_try_get_tx_hash(result: &mut CrossBoundaryResult, raw_tx: &str) -> String;

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
            raw_tx: &str,
            native_hash: &str,
            token_id: u64,
            out: bool,
            pre_validate: bool,
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
        fn evm_is_smart_contract_in_q(
            result: &mut CrossBoundaryResult,
            address: &str,
            queue_id: u64,
        ) -> bool;
        fn evm_try_get_tx_sender_info_from_raw_tx(
            result: &mut CrossBoundaryResult,
            raw_tx: &str,
        ) -> TxSenderInfo;
    }
}
