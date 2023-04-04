use ain_evm::*;
use ain_grpc::*;

use ain_evm_runtime::RUNTIME;
use std::error::Error;

#[cxx::bridge]
mod ffi {
    extern "Rust" {
        fn evm_add_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_sub_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_validate_raw_tx(tx: &str) -> Result<bool>;

        fn evm_get_context() -> u64;
        fn evm_discard_context(context: u64);
        fn evm_queue_tx(context: u64) -> Result<bool>;
        fn evm_finalise(context: u64, update_state: bool) -> Result<Vec<u8>>;

        fn init_runtime();
        fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<()>;
        fn stop_runtime();

        fn create_and_sign_tx(
            chain_id: u64,
            nonce: [u8; 32],
            gas_price: [u8; 32],
            gas_limit: [u8; 32],
            to: [u8; 20],
            value: [u8; 32],
            input: Vec<u8>,
            priv_key: [u8; 32],
        ) -> Vec<u8>;
    }
}

pub fn evm_add_balance(address: &str, amount: i64) -> Result<(), Box<dyn Error>> {
    RUNTIME.evm.add_balance(address, amount)
}

pub fn evm_sub_balance(address: &str, amount: i64) -> Result<(), Box<dyn Error>> {
    RUNTIME.evm.sub_balance(address, amount)
}

pub fn evm_validate_raw_tx(tx: &str) -> Result<bool, Box<dyn Error>> {
    match RUNTIME.evm.validate_raw_tx(tx) {
        Ok(_) => Ok(true),
        Err(_) => Ok(false),
    }
}

pub fn evm_get_context() -> u64 {
    // TODO Generate unique contexts.
    1
}

fn evm_discard_context(context: u64) {
    // TODO discard
}

fn evm_queue_tx(context: u64) -> Result<bool, Box<dyn Error>> {
    Ok(true)
}

fn evm_finalise(context: u64, update_state: bool) -> Result<Vec<u8>, Box<dyn Error>> {
    let block_and_failed_txs: Vec<u8> = Vec::new();
    Ok(block_and_failed_txs)
}

