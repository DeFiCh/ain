use ain_evm::runtime::RUNTIME;
use ain_evm::transaction::system::{DeployContractData, SystemTx};
use ain_evm::tx_queue::QueueTx;
use log::debug;
use primitive_types::H160;
use std::error::Error;
use std::str::FromStr;

/// Creates a deploy token system transaction in mempool
pub fn deploy_dst20(
    native_hash: [u8; 32],
    context: u64,
    name: String,
    symbol: String,
    token_id: String,
) -> Result<(), Box<dyn Error>> {
    let address = dst20_address_from_token_id(token_id)?;
    debug!("Deploying to address {:#?}", address);

    let system_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        name,
        symbol,
        address,
    }));

    RUNTIME.handlers.queue_tx(context, system_tx, native_hash)?;

    Ok(())
}

pub fn dst20_address_from_token_id(token_id: String) -> Result<H160, Box<dyn Error>> {
    let number_str = format!("{:x}", token_id.parse::<u64>()?);
    let padded_number_str = format!("{:0>38}", number_str);
    let final_str = format!("ff{}", padded_number_str);

    Ok(H160::from_str(&final_str)?)
}
