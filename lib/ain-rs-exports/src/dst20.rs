use ain_evm::runtime::RUNTIME;
use ain_evm::transaction::system::{DeployContractData, SystemTx};
use ain_evm::tx_queue::QueueTx;
use ethereum::{EIP1559TransactionMessage, TransactionAction, TransactionV2};
use ethers::abi::{Abi, Token};
use ethers::types::Bytes;
use ethers::utils::keccak256;
use libsecp256k1::SecretKey;
use log::debug;
use primitive_types::{H160, H256, U256};
use std::error::Error;
use std::str::FromStr;
use std::sync::RwLock;

/// Creates a deploy token system transaction in mempool
pub fn deploy_dst20(
    native_hash: [u8; 32],
    context: u64,
    apply_changes: bool,
    name: String,
    symbol: String,
) -> Result<(), Box<dyn Error>> {
    let system_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        name,
        symbol,
    }));

    RUNTIME
        .handlers
        .queue_tx(context, system_tx.into(), native_hash)?;

    Ok(())
}
