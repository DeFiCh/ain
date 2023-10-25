mod core;
mod evm;
mod prelude;

use ain_evm::blocktemplate::BlockTemplate;

use crate::{core::*, evm::*};

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
    pub struct TxMinerInfo {
        pub address: String,
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
            websockets_addr: &str,
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
        pub use_nonce: bool,
        pub nonce: u64,
    }

    pub struct TransferDomainInfo {
        pub from: String,
        pub to: String,
        pub native_address: String,
        pub direction: bool,
        pub value: u64,
        pub token_id: u32,
    }

    #[derive(Default)]
    pub struct CreateTxResult {
        pub tx: Vec<u8>,
        pub nonce: u64,
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
        pub tx_hash: String,
    }

    extern "Rust" {
        type BlockTemplateWrapper;
        // In-fallible functions
        //
        // If they are fallible, it's a TODO to changed and move later
        // so errors are propogated up properly.
        fn evm_try_get_balance(result: &mut CrossBoundaryResult, address: &str) -> u64;

        fn evm_try_unsafe_create_template(
            result: &mut CrossBoundaryResult,
            dvm_block: u64,
            miner_address: &str,
            difficulty: u32,
            timestamp: u64,
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
            mnview_ptr: usize,
        );

        fn evm_try_unsafe_get_next_valid_nonce_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &BlockTemplateWrapper,
            address: &str,
        ) -> u64;

        fn evm_try_unsafe_remove_txs_above_hash_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            target_hash: String,
        ) -> Vec<String>;

        fn evm_try_unsafe_add_balance_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            raw_tx: &str,
            native_hash: &str,
        );

        fn evm_try_unsafe_sub_balance_in_template(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            raw_tx: &str,
            native_hash: &str,
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
            native_hash: &str,
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
            from_address: &str,
            nonce: u64,
        );

        fn evm_try_get_block_hash_by_number(
            result: &mut CrossBoundaryResult,
            height: u64,
        ) -> String;

        fn evm_try_get_block_number_by_hash(result: &mut CrossBoundaryResult, hash: &str) -> u64;

        fn evm_try_get_block_header_by_hash(
            result: &mut CrossBoundaryResult,
            hash: &str,
        ) -> EVMBlockHeader;

        fn evm_try_get_tx_by_hash(
            result: &mut CrossBoundaryResult,
            tx_hash: &str,
        ) -> EVMTransaction;

        fn evm_try_get_tx_hash(result: &mut CrossBoundaryResult, raw_tx: &str) -> String;

        fn evm_try_unsafe_create_dst20(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            native_hash: &str,
            name: &str,
            symbol: &str,
            token_id: u64,
        );

        fn evm_try_unsafe_bridge_dst20(
            result: &mut CrossBoundaryResult,
            block_template: &mut BlockTemplateWrapper,
            raw_tx: &str,
            native_hash: &str,
            token_id: u64,
            out: bool,
        );

        fn evm_try_unsafe_is_smart_contract_in_template(
            result: &mut CrossBoundaryResult,
            address: &str,
            block_template: &BlockTemplateWrapper,
        ) -> bool;

        fn evm_try_get_tx_miner_info_from_raw_tx(
            result: &mut CrossBoundaryResult,
            raw_tx: &str,
        ) -> TxMinerInfo;

        fn evm_try_dispatch_pending_transactions_event(
            result: &mut CrossBoundaryResult,
            raw_tx: &str,
        );
    }
}
