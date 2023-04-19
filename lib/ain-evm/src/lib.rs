mod block;
mod cache;
mod ecrecover;
pub mod evm;
pub mod executor;
pub mod handler;
pub mod runtime;
pub mod storage;
pub mod traits;
pub mod transaction;
mod tx_queue;

use ethereum::{EnvelopedEncodable, TransactionAction, TransactionSignature};
use primitive_types::{H160, H256, U256};
use transaction::{LegacyUnsignedTransaction, TransactionError, LOWER_H256};

pub fn create_and_sign_tx(
    chain_id: u64,
    nonce: [u8; 32],
    gas_price: [u8; 32],
    gas_limit: [u8; 32],
    to: [u8; 20],
    value: [u8; 32],
    input: Vec<u8>,
    priv_key: [u8; 32],
) -> Result<Vec<u8>, TransactionError> {
    let to_action = if to.is_empty() {
        TransactionAction::Create
    } else {
        TransactionAction::Call(H160::from_slice(&to))
    };

    let nonce_u256 = U256::from(nonce);
    let gas_price_u256 = U256::from(gas_price);
    let gas_limit_u256 = U256::from(gas_limit);
    let value_u256 = U256::from(value);

    // Create
    let t = LegacyUnsignedTransaction {
        nonce: nonce_u256,
        gas_price: gas_price_u256,
        gas_limit: gas_limit_u256,
        action: to_action,
        value: value_u256,
        input,
        // Dummy sig for now. Needs 27, 28 or > 36 for valid v.
        sig: TransactionSignature::new(27, LOWER_H256, LOWER_H256).unwrap(),
    };

    // Sign
    let priv_key_h256 = H256::from(priv_key);
    let signed = t.sign(&priv_key_h256, chain_id)?;

    Ok(signed.encode().into())
}
