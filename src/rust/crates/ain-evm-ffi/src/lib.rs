use ain_evm::*;
use ain_grpc::*;

use ain_evm_runtime::RUNTIME;
use std::error::Error;

#[cxx::bridge]
mod ffi {
    extern "Rust" {
        fn evm_add_balance(context: u64, address: &str, amount: [u8; 32]) -> Result<()>;
        fn evm_sub_balance(context: u64, address: &str, amount: [u8; 32]) -> Result<bool>;
        fn evm_validate_raw_tx(tx: &str) -> Result<bool>;

        fn evm_get_context() -> u64;
        fn evm_discard_context(context: u64);
        fn evm_queue_tx(context: u64, raw_tx: &str) -> Result<bool>;
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

pub fn evm_add_balance(context: u64, address: &str, amount: [u8; 32]) -> Result<(), Box<dyn Error>> {
    RUNTIME.evm.add_balance(context, address, amount.into())
}

pub fn evm_sub_balance(context: u64, address: &str, amount: [u8; 32]) -> Result<bool, Box<dyn Error>> {
    match RUNTIME.evm.sub_balance(context, address, amount.into()) {
        Ok(_) => Ok(true),
        Err(_) => Ok(false),
    }
}

pub fn evm_validate_raw_tx(tx: &str) -> Result<bool, Box<dyn Error>> {
    match RUNTIME.evm.validate_raw_tx(tx) {
        Ok(_) => Ok(true),
        Err(_) => Ok(false),
    }
}

pub fn evm_get_context() -> u64 {
    RUNTIME.evm.get_context()
}

fn evm_discard_context(context: u64) {
    // TODO discard
    RUNTIME.evm.discard_context(context)
}

fn evm_queue_tx(context: u64, raw_tx: &str) -> Result<bool, Box<dyn Error>> {
    match RUNTIME.evm.queue_tx(context, raw_tx) {
        Ok(_) => Ok(true),
        Err(_) => Ok(false),
    }
}

fn evm_finalise(context: u64, update_state: bool) -> Result<Vec<u8>, Box<dyn Error>> {
    let block_and_failed_txs: Vec<u8> = Vec::new();
    Ok(block_and_failed_txs)
}
