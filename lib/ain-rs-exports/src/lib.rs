mod core;
mod debug;
mod evm;
mod prelude;
mod util;

use ain_evm::blocktemplate::BlockTemplate;

use crate::{core::*, debug::*, evm::*, util::*};

pub struct BlockTemplateWrapper(Option<BlockTemplate>);

impl BlockTemplateWrapper {
    const ERROR: &'static str = "Inner block template is None";

    fn get_inner(&self) -> Result<&BlockTemplate, &'static str> {
        self.0.as_ref().ok_or(Self::ERROR)
    }

    fn get_inner_mut(&mut self) -> Result<&mut BlockTemplate, &'static str> {
        self.0.as_mut().ok_or(Self::ERROR)
    }
}

#[cxx::bridge]
pub mod ffi {
    // =========  FFI ==========
    pub struct CrossBoundaryResult {
        pub ok: bool,
        pub reason: String,
    }

    // =========  Util ==========
    extern "Rust" {
        fn rs_try_from_utf8(result: &mut CrossBoundaryResult, string: &'static [u8]) -> String;
    }

    // =========  Core ==========
    extern "Rust" {
        fn ain_rs_preinit(result: &mut CrossBoundaryResult);
        fn ain_rs_init_logging(result: &mut CrossBoundaryResult);
        fn ain_rs_init_core_services(result: &mut CrossBoundaryResult);
        fn ain_rs_wipe_evm_folder(result: &mut CrossBoundaryResult);
        fn ain_rs_stop_core_services(result: &mut CrossBoundaryResult);

        // Networking
        fn ain_rs_init_network_json_rpc_service(result: &mut CrossBoundaryResult, addr: String);
        fn ain_rs_init_network_grpc_service(result: &mut CrossBoundaryResult, addr: String);
        fn ain_rs_init_network_subscriptions_service(
            result: &mut CrossBoundaryResult,
            addr: String,
        );
        fn ain_rs_stop_network_services(result: &mut CrossBoundaryResult);
    }

    // ========== Block ==========
    #[derive(Default)]
    pub struct EVMBlockHeader {
        pub parent_hash: [u8; 32],
        pub beneficiary: [u8; 20],
        pub state_root: [u8; 32],
        pub receipts_root: [u8; 32],
        pub number: u64,
        pub gas_limit: u64,
        pub gas_used: u64,
        pub timestamp: u64,
        pub extra_data: Vec<u8>,
        pub mix_hash: [u8; 32],
        pub nonce: u64,
        pub base_fee: u64,
    }

    // ========== Transaction ==========
    #[derive(Default)]
    pub struct EVMTransaction {
        // EIP-2718 transaction type: legacy - 0x0, EIP2930 - 0x1, EIP1559 - 0x2
        pub tx_type: u8,
        pub hash: [u8; 32],
        pub sender: [u8; 20],
        pub nonce: u64,
        pub gas_price: u64,
        pub gas_limit: u64,
        pub max_fee_per_gas: u64,
        pub max_priority_fee_per_gas: u64,
        pub create_tx: bool,
        pub to: [u8; 20],
        pub value: u64,
        pub data: Vec<u8>,
    }

    #[derive(Default)]
    pub struct TxMinerInfo {
        pub address: [u8; 20],
        pub nonce: u64,
        pub tip_fee: u64,
        pub min_rbf_tip_fee: u64,
    }

    // ========== Governance Variable ==========
    #[derive(Default)]
    pub struct GovVarKeyDataStructure {
        pub category: u8,
        pub category_id: u32,
        pub key: u32,
        pub key_id: u32,
    }

    // ========== EVM ==========

    pub struct CreateTransactionContext {
        pub chain_id: u64,
        pub nonce: u64,
        pub gas_price: u64,
        pub gas_limit: u64,
        pub to: [u8; 20],
        pub value: u64,
        pub input: Vec<u8>,
        pub priv_key: [u8; 32],
    }

    pub struct CreateTransferDomainContext {
        pub from: [u8; 20],
        pub to: [u8; 20],
        pub native_address: String,
        pub direction: bool,
        pub value: u64,
        pub token_id: u32,
        pub chain_id: u64,
        pub priv_key: [u8; 32],
        pub use_nonce: bool,
        pub nonce: u64,
    }

    pub struct TransferDomainInfo {
        pub from: [u8; 20],
        pub to: [u8; 20],
        pub native_address: String,
        pub direction: bool,
        pub value: u64,
        pub token_id: u32,
    }

    pub struct DST20TokenInfo {
        pub id: u64,
        pub name: String,
        pub symbol: String,
    }

    #[derive(Default)]
    pub struct CreateTxResult {
        pub tx: Vec<u8>,
        pub nonce: u64,
    }

    #[derive(Default)]
    pub struct FinalizeBlockCompletion {
        pub block_hash: [u8; 32],
        pub total_burnt_fees: u64,
        pub total_priority_fees: u64,
        pub block_number: u64,
    }

    #[derive(Default)]
    pub struct ValidateTxCompletion {
        pub tx_hash: [u8; 32],
    }

    extern "Rust" {
        type BlockTemplateWrapper;
        // In-fallible functions
        //
        // If they are fallible, it's a TODO to changed and move later
        // so errors are propogated up properly.
        fn evm_try_get_balance(result: &mut CrossBoundaryResult, address: [u8; 20]) -> u64;

        fn evm_try_unsafe_create_template(
            result: &mut CrossBoundaryResult,
            dvm_block: u64,
            miner_address: &str,
            difficulty: u32,
            timestamp: u64,
            mnview_ptr: usize,
        ) -> &'static mut BlockTemplateWrapper;

        fn evm_try_unsafe_remove_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
        );

        fn evm_try_disconnect_latest_block(result: &mut CrossBoundaryResult);

        // Failible functions
        // Has to take CrossBoundaryResult as first param
        // Has to start with try_ / evm_try
        fn evm_try_unsafe_update_state_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
        );

        fn evm_try_unsafe_get_next_valid_nonce_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &BlockTemplateWrapper,
            address: [u8; 20],
        ) -> u64;

        fn evm_try_unsafe_remove_txs_above_hash_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            target_hash: [u8; 32],
        );

        fn evm_try_unsafe_add_balance_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            raw_tx: &str,
            native_hash: [u8; 32],
        );

        fn evm_try_unsafe_sub_balance_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            raw_tx: &str,
            native_hash: [u8; 32],
        ) -> bool;

        fn evm_try_unsafe_validate_raw_tx_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &BlockTemplateWrapper,
            raw_tx: &str,
        );

        fn evm_try_unsafe_validate_transferdomain_tx_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &BlockTemplateWrapper,
            raw_tx: &str,
            context: TransferDomainInfo,
        );

        fn evm_try_unsafe_push_tx_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            raw_tx: &str,
            native_hash: [u8; 32],
        ) -> ValidateTxCompletion;

        fn evm_try_unsafe_construct_block_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            is_miner: bool,
        ) -> FinalizeBlockCompletion;

        fn evm_try_unsafe_commit_block(
            result: &mut CrossBoundaryResult,
            block_template: &BlockTemplateWrapper,
        );

        fn evm_try_unsafe_handle_attribute_apply(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            attribute_type: GovVarKeyDataStructure,
            value: Vec<u8>,
        ) -> bool;

        fn evm_try_create_and_sign_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransactionContext,
        ) -> CreateTxResult;

        fn evm_try_create_and_sign_transfer_domain_tx(
            result: &mut CrossBoundaryResult,
            ctx: CreateTransferDomainContext,
        ) -> CreateTxResult;

        fn evm_try_store_account_nonce(
            result: &mut CrossBoundaryResult,
            from_address: [u8; 20],
            nonce: u64,
        );

        fn evm_try_get_block_hash_by_number(
            result: &mut CrossBoundaryResult,
            height: u64,
        ) -> [u8; 32];

        fn evm_try_get_block_number_by_hash(
            result: &mut CrossBoundaryResult,
            hash: [u8; 32],
        ) -> u64;

        fn evm_try_get_block_header_by_hash(
            result: &mut CrossBoundaryResult,
            hash: [u8; 32],
        ) -> EVMBlockHeader;

        fn evm_try_get_latest_block_hash(result: &mut CrossBoundaryResult) -> [u8; 32];

        fn evm_try_get_tx_by_hash(
            result: &mut CrossBoundaryResult,
            tx_hash: [u8; 32],
        ) -> EVMTransaction;

        fn evm_try_get_tx_hash(result: &mut CrossBoundaryResult, raw_tx: &str) -> [u8; 32];

        fn evm_try_unsafe_make_signed_tx(result: &mut CrossBoundaryResult, raw_tx: &str) -> usize;

        fn evm_try_unsafe_cache_signed_tx(
            result: &mut CrossBoundaryResult,
            raw_tx: &str,
            instance: usize,
        );

        fn evm_try_unsafe_create_dst20(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            native_hash: [u8; 32],
            token: DST20TokenInfo,
        );

        fn evm_try_unsafe_bridge_dst20(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            raw_tx: &str,
            native_hash: [u8; 32],
            token_id: u64,
            out: bool,
        );

        fn evm_try_unsafe_is_smart_contract_in_template(
            result: &mut CrossBoundaryResult,
            address: [u8; 20],
            block_template: &BlockTemplateWrapper,
        ) -> bool;

        fn evm_try_get_tx_miner_info_from_raw_tx(
            result: &mut CrossBoundaryResult,
            raw_tx: &str,
            mnview_ptr: usize,
        ) -> TxMinerInfo;

        fn evm_try_dispatch_pending_transactions_event(
            result: &mut CrossBoundaryResult,
            raw_tx: &str,
        );

        fn evm_try_flush_db(result: &mut CrossBoundaryResult);

        fn evm_try_unsafe_rename_dst20(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            native_hash: [u8; 32],
            token: DST20TokenInfo,
        );
    }

    // =========  Debug ==========
    extern "Rust" {
        fn debug_dump_db(
            result: &mut CrossBoundaryResult,
            arg: String,
            start: String,
            limit: String,
        ) -> String;

        fn debug_log_account_states(result: &mut CrossBoundaryResult) -> String;
    }
}
