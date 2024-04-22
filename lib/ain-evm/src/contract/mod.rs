pub mod dst20;

use ain_contracts::{
    get_dfi_instrinics_registry_contract, get_dfi_intrinsics_v1_contract,
    get_dfi_reserved_contract, get_transfer_domain_contract, get_transfer_domain_v1_contract,
    Contract, FixedContract, IMPLEMENTATION_SLOT,
};
use anyhow::format_err;
use ethbloom::Bloom;
use ethereum::{
    EIP1559ReceiptData, LegacyTransaction, ReceiptV3, TransactionAction, TransactionSignature,
    TransactionV2,
};
use ethereum_types::{H160, H256, U256};
use log::trace;
use sha3::{Digest, Keccak256};

use crate::{
    backend::EVMBackend,
    bytes::Bytes,
    executor::AinExecutor,
    transaction::{system::TransferDirection, SignedTx, LOWER_H256},
    Result,
};

pub fn u256_to_h256(input: U256) -> H256 {
    let mut bytes = [0_u8; 32];
    input.to_big_endian(&mut bytes);

    H256::from(bytes)
}

pub fn h160_to_h256(h160: H160) -> H256 {
    let mut h256 = H256::zero();
    h256.0[12..].copy_from_slice(&h160.0);
    h256
}

pub fn get_abi_encoded_string(input: &str) -> H256 {
    let length = input.len();

    let mut storage_value = H256::default();
    storage_value.0[31] = (length * 2) as u8; // safe due to character limits on token name/symbol on DVM
    storage_value.0[..length].copy_from_slice(input.as_bytes());

    storage_value
}

pub fn get_address_storage_index(slot: H256, address: H160) -> H256 {
    // padded key
    let key = H256::from(address);

    // keccak256(padded key + padded slot)
    let mut hasher = Keccak256::new();
    hasher.update(key.as_fixed_bytes());
    hasher.update(slot.as_fixed_bytes());
    let hash_result = hasher.finalize();

    let mut index_bytes = [0u8; 32];
    index_bytes.copy_from_slice(&hash_result);

    H256::from(index_bytes)
}

pub fn get_uint_storage_index(slot: H256, num: u64) -> H256 {
    // padded key
    let key = H256::from_low_u64_be(num);

    // keccak256(padded key + padded slot)
    let mut hasher = Keccak256::new();
    hasher.update(key.as_fixed_bytes());
    hasher.update(slot.as_fixed_bytes());
    let hash_result = hasher.finalize();

    let mut index_bytes = [0u8; 32];
    index_bytes.copy_from_slice(&hash_result);

    H256::from(index_bytes)
}

pub struct DeployContractInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
    pub bytecode: Bytes,
}

pub fn dfi_intrinsics_registry_deploy_info(addresses: Vec<H160>) -> DeployContractInfo {
    let FixedContract {
        contract,
        fixed_address,
    } = get_dfi_instrinics_registry_contract();

    let storage = addresses
        .into_iter()
        .enumerate()
        .map(|(index, address)| {
            (
                get_uint_storage_index(H256::from_low_u64_be(0), index as u64),
                h160_to_h256(address),
            )
        })
        .collect();

    DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage,
    }
}
/// Returns address, bytecode and storage with incremented count for the counter contract
pub fn dfi_intrinsics_v1_deploy_info(
    dvm_block_number: u64,
    evm_block_number: U256,
) -> Result<DeployContractInfo> {
    let FixedContract {
        contract,
        fixed_address,
        ..
    } = get_dfi_intrinsics_v1_contract();

    Ok(DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage: vec![
            (H256::from_low_u64_be(0), u256_to_h256(U256::one())),
            (H256::from_low_u64_be(1), u256_to_h256(evm_block_number)),
            (
                H256::from_low_u64_be(2),
                u256_to_h256(U256::from(dvm_block_number)),
            ),
        ],
    })
}

pub fn transfer_domain_deploy_info(implementation_address: H160) -> Result<DeployContractInfo> {
    let FixedContract {
        contract,
        fixed_address,
        ..
    } = get_transfer_domain_contract();

    Ok(DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage: vec![(IMPLEMENTATION_SLOT, h160_to_h256(implementation_address))],
    })
}

/// Returns transfer domain address, bytecode and null storage
pub fn transfer_domain_v1_contract_deploy_info() -> DeployContractInfo {
    let FixedContract {
        contract,
        fixed_address,
    } = get_transfer_domain_v1_contract();

    DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage: Vec::new(),
    }
}

pub fn bridge_dfi(
    backend: &EVMBackend,
    amount: U256,
    direction: TransferDirection,
) -> Result<Vec<(H256, H256)>> {
    let FixedContract { fixed_address, .. } = get_transfer_domain_contract();

    let total_supply_index = H256::from_low_u64_be(1);
    let total_supply = backend.get_contract_storage(fixed_address, total_supply_index)?;

    let new_total_supply = if direction == TransferDirection::EvmOut {
        total_supply.checked_sub(amount)
    } else {
        total_supply.checked_add(amount)
    };

    let new_total_supply =
        new_total_supply.ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    Ok(vec![(total_supply_index, u256_to_h256(new_total_supply))])
}

pub fn reserve_intrinsics_namespace(executor: &mut AinExecutor) -> Result<()> {
    let Contract {
        runtime_bytecode, ..
    } = get_dfi_reserved_contract();
    let addresses = (0..128)
        .map(|token_id| ain_contracts::intrinsics_address_from_id(token_id).unwrap())
        .collect::<Vec<H160>>();

    for address in addresses {
        trace!(
            "[reserve_intrinsics_namespace] Deploying address to {:#?}",
            address
        );
        executor.deploy_contract(address, runtime_bytecode.clone().into(), Vec::new())?;
    }

    Ok(())
}

pub fn deploy_contract_tx(bytecode: Vec<u8>, base_fee: &U256) -> Result<(SignedTx, ReceiptV3)> {
    let tx = TransactionV2::Legacy(LegacyTransaction {
        nonce: U256::zero(),
        gas_price: *base_fee,
        gas_limit: U256::from(u64::MAX),
        action: TransactionAction::Create,
        value: U256::zero(),
        input: bytecode,
        signature: TransactionSignature::new(27, LOWER_H256, LOWER_H256)
            .ok_or(format_err!("Invalid transaction signature format"))?,
    })
    .try_into()?;

    let receipt = get_default_successful_receipt();

    Ok((tx, receipt))
}

pub fn rename_contract_tx(token_id: u64, base_fee: &U256) -> Result<(Box<SignedTx>, ReceiptV3)> {
    let tx = TransactionV2::Legacy(LegacyTransaction {
        nonce: U256::from(token_id),
        gas_price: *base_fee,
        gas_limit: U256::from(u64::MAX),
        action: TransactionAction::Create,
        value: U256::zero(),
        input: Vec::new(),
        signature: TransactionSignature::new(27, LOWER_H256, LOWER_H256)
            .ok_or(format_err!("Invalid transaction signature format"))?,
    })
    .try_into()?;

    let receipt = get_default_successful_receipt();

    Ok((Box::new(tx), receipt))
}

fn get_default_successful_receipt() -> ReceiptV3 {
    ReceiptV3::Legacy(EIP1559ReceiptData {
        status_code: 1u8,
        used_gas: U256::zero(),
        logs_bloom: Bloom::default(),
        logs: Vec::new(),
    })
}
