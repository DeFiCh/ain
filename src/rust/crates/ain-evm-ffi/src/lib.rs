use ain_evm::*;
use ain_grpc::*;

#[cxx::bridge]
mod ffi {
    extern "Rust" {
        fn evm_add_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_sub_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_send_raw_tx(tx: &str) -> Result<()>;

        fn init_runtime();
        fn start_servers(json_addr: &str, grpc_addr: &str) -> Result<()>;
        fn stop_runtime();
    }
}
