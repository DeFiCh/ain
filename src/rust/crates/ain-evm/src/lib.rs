mod transaction;

use ain_evm_runtime::RUNTIME;
use std::error::Error;

use primitive_types::{H160, H256, U256};
use transaction::LegacyUnsignedTransaction;
use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature};

#[cxx::bridge]
mod server {
    extern "Rust" {
        fn evm_add_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_sub_balance(address: &str, amount: i64) -> Result<()>;
        fn evm_send_raw_tx(tx: &str) -> Result<()>;

        fn create_and_sign_tx(
            chain_id: u64,
            nonce: [u8; 32],
            gas_price: [u8; 32],
            gas_limit: [u8; 32],
            to: [u8; 20],
            value: [u8; 32],
            input: &str,
            priv_key: [u8; 32],
        ) -> String;
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

fn create_and_sign_tx(
    chain_id: u64,
    nonce: [u8; 32],
    gas_price: [u8; 32],
    gas_limit: [u8; 32],
    to: [u8; 20],
    value: [u8; 32],
    input: &str,
    priv_key: [u8; 32],
) -> String {

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

    // Lowest acceptable value for r and s in sig.
    const LOWER: H256 = H256([
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
    ]);

    let sig = TransactionSignature::new(27,LOWER, LOWER).unwrap();

    // Create

    let t = LegacyUnsignedTransaction{
        nonce: nonce_u256,
        gas_price: gas_price_u256,
        gas_limit: gas_limit_u256,
        action: to_action,
        value: value_u256,
        input: hex::decode(input).expect("Decoding failed"),
        // Dummy sig for now. Needs 27, 28 or > 36 for valid v.
        sig: TransactionSignature::new(27,LOWER, LOWER).unwrap(),
    };

    let priv_key_h256 = H256::from(priv_key);

    // Sign
    let signed = t.sign(&priv_key_h256, chain_id);

    hex::encode(signed.encode())
}
