mod transaction;

use ain_evm_runtime::RUNTIME;
use std::error::Error;

use primitive_types::{H160, H256, U256};
use transaction::{LOWER_H256, LegacyUnsignedTransaction};
use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature};

pub fn evm_add_balance(address: &str, amount: i64) -> Result<(), Box<dyn Error>> {
    RUNTIME.handlers.evm.add_balance(address, amount)
}

pub fn evm_sub_balance(address: &str, amount: i64) -> Result<(), Box<dyn Error>> {
    RUNTIME.handlers.evm.sub_balance(address, amount)
}

pub fn evm_send_raw_tx(_tx: &str) -> Result<(), Box<dyn Error>> {
    Ok(())
}

pub fn create_and_sign_tx(
    chain_id: u64,
    nonce: [u8; 32],
    gas_price: [u8; 32],
    gas_limit: [u8; 32],
    to: [u8; 20],
    value: [u8; 32],
    input: Vec<u8>,
    priv_key: [u8; 32],
) -> Vec<u8> {

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
        // Dummy sig for now. Needs 27, 28 or > 36 for valid v.
        sig: TransactionSignature::new(27,LOWER_H256, LOWER_H256).unwrap(),
    };
    
    // Sign
    let priv_key_h256 = H256::from(priv_key);
    let signed = t.sign(&priv_key_h256, chain_id);

    signed.encode().into()
}
