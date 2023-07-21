use crate::ffi::CrossBoundaryResult;
use crate::prelude::{cross_boundary_error_return, cross_boundary_success};
use ain_evm::bytes::Bytes;
use ain_evm::services::SERVICES;
use ain_evm::transaction::system::{DeployContractData, SystemTx};
use ain_evm::txqueue::QueueTx;
use anyhow::anyhow;
use primitive_types::{H160, H256, U256};
use std::error::Error;
use std::str::FromStr;

pub fn evm_deploy_counter_contract(result: &mut CrossBoundaryResult, context: u64) {
    match deploy_counter_contract(context) {
        Ok(_) => cross_boundary_success(result),
        Err(e) => cross_boundary_error_return(result, e.to_string()),
    }
}

fn deploy_counter_contract(context: u64) -> Result<(), Box<dyn Error>> {
    let address = H160::from_str("0x0000000000000000000000000000000000000301").unwrap();
    let bytecode = get_bytecode(include_str!("../counter_contract/output/bytecode.json"))?;
    let (_, latest_block_number) = SERVICES
        .evm
        .block
        .get_latest_block_hash_and_number()
        .unwrap_or_default();
    let count = U256::from(
        SERVICES
            .evm
            .core
            .get_storage_at(address, U256::one(), latest_block_number)?
            .unwrap_or_default()
            .as_slice(),
    );

    let system_tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
        bytecode,
        storage: vec![(H256::from_low_u64_be(1), u256_to_h256(count + U256::one()))],
        address,
    }));

    SERVICES
        .evm
        .queue_tx(context, system_tx, Default::default(), 0)?;

    Ok(())
}

fn u256_to_h256(input: U256) -> H256 {
    let mut bytes = [0_u8; 32];
    input.to_big_endian(&mut bytes);

    H256::from(bytes)
}

fn get_abi_encoded_string(input: &str) -> H256 {
    let length = input.len();

    let mut storage_value = H256::default();
    storage_value.0[31] = (length * 2) as u8;
    storage_value.0[..length].copy_from_slice(input.as_bytes());

    storage_value
}

fn get_bytecode(input: &str) -> Result<Bytes, Box<dyn Error>> {
    let bytecode_json: serde_json::Value = serde_json::from_str(input)?;
    let bytecode_raw = bytecode_json["object"]
        .as_str()
        .ok_or_else(|| anyhow!("Bytecode object not available".to_string()))?;

    Ok(Bytes::from(
        hex::decode(&bytecode_raw[2..]).map_err(|e| anyhow!(e.to_string()))?,
    ))
}
