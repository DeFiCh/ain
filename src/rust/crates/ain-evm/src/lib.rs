mod transaction;

use ain_evm_runtime::RUNTIME;
use std::error::Error;

use primitive_types::{H160, H256, U256};
use transaction::LegacyUnsignedTransaction;
use ethereum::{TransactionAction, TransactionSignature};

#[cxx::bridge]
mod server {
    extern "Rust" {
        fn evm_add_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_sub_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_send_raw_tx(tx: &str) -> Result<()>;

        fn create_sign_and_execute_evm_tx(
            chain_id: u64,
            nonce: [u8; 32],
            gas_price: [u8; 32],
            gas_limit: [u8; 32],
            to: Vec<u8>,
            value: [u8; 32],
            input: Vec<u8>,
            priv_key: [u8; 32],
        );
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

fn create_sign_and_execute_evm_tx(
    chain_id: u64,
    nonce: [u8; 32],
    gas_price: [u8; 32],
    gas_limit: [u8; 32],
    to: Vec<u8>,
    value: [u8; 32],
    input: Vec<u8>,
    priv_key: [u8; 32],
) {
    let to_action : TransactionAction;
    if to.is_empty() {
        to_action = TransactionAction::Create;
    } else {
        to_action = TransactionAction::Call(H160::from_slice(&to));
    }

    let nonce_u256 = U256::from(nonce);
    let gas_price_u256 = U256::from(gas_price);
    let gas_limit_u256 = U256::from(gas_limit);
    let value_u256 = U256::from(value);

    // Create
    let t = LegacyUnsignedTransaction{
        nonce: nonce_u256,
        gas_price: gas_price_u256,
        gas_limit: gas_limit_u256,
        action: to_action,
        value: value_u256,
        input,
        sig: TransactionSignature::new(0,H256::zero(), H256::zero()).unwrap(),
    };

    let priv_key_h256 = H256::from(priv_key);

    // Sign
    t.sign(&priv_key_h256, chain_id);

    // Execute
    // TODO Jeremy
}
