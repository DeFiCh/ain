use ain_evm_runtime::RUNTIME;
use std::error::Error;

#[cxx::bridge]
mod server {
    extern "Rust" {
        fn evm_add_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_sub_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_send_raw_tx(tx: &str) -> Result<()>;
    }
}

fn evm_add_balance(address: &str, amount: i64) -> Result<(), Box<dyn Error>> {
    RUNTIME.evm.add_balance(address, amount)
}

fn evm_sub_balance(address: &str, amount: i64) -> Result<(), Box<dyn Error>> {
    RUNTIME.evm.sub_balance(address, amount)
}

fn evm_send_raw_tx(_tx: &str) -> Result<(), Box<dyn Error>> {
    Ok(())
}
