use ain_contracts::{
    get_dst20_contract, get_intrinsic_contract, get_reserved_contract, get_transferdomain_contract,
    Contract, FixedContract,
};
use anyhow::format_err;
use ethereum_types::{H160, H256, U256};
use log::debug;
use sha3::{Digest, Keccak256};

use crate::{backend::EVMBackend, bytes::Bytes, Result};

pub fn u256_to_h256(input: U256) -> H256 {
    let mut bytes = [0_u8; 32];
    input.to_big_endian(&mut bytes);

    H256::from(bytes)
}

pub fn get_abi_encoded_string(input: &str) -> H256 {
    let length = input.len();

    let mut storage_value = H256::default();
    storage_value.0[31] = (length * 2) as u8;
    storage_value.0[..length].copy_from_slice(input.as_bytes());

    storage_value
}

pub fn get_address_storage_index(address: H160) -> H256 {
    // padded slot, slot for our contract is 0
    let slot = H256::zero();

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

pub struct DeployContractInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
    pub bytecode: Bytes,
}

pub struct DST20BridgeInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
}

/// Returns address, bytecode and storage with incremented count for the counter contract
pub fn counter_contract(
    backend: &EVMBackend,
    dvm_block_number: u64,
    evm_block_number: U256,
) -> Result<DeployContractInfo> {
    let FixedContract {
        contract,
        fixed_address,
        ..
    } = get_intrinsic_contract();
    let count =
        backend.get_contract_storage(fixed_address, u256_to_h256(U256::one()).as_bytes())?;

    debug!("Count: {:#x}", count + U256::one());

    Ok(DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.bytecode),
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

/// Returns transfer domain address, bytecode and null storage
pub fn transfer_domain_contract() -> Result<DeployContractInfo> {
    let FixedContract {
        contract,
        fixed_address,
    } = get_transferdomain_contract();

    Ok(DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.bytecode),
        storage: Vec::new(),
    })
}

pub fn dst20_contract(
    backend: &EVMBackend,
    address: H160,
    name: String,
    symbol: String,
) -> Result<DeployContractInfo> {
    match backend.get_account(&address) {
        None => {}
        Some(account) => {
            let Contract { codehash, .. } = get_reserved_contract();
            if account.code_hash != codehash {
                return Err(format_err!("Token {symbol} address is already in use").into());
            }
        }
    }

    let Contract { bytecode, .. } = get_dst20_contract();
    let storage = vec![
        (
            H256::from_low_u64_be(3),
            get_abi_encoded_string(name.as_str()),
        ),
        (
            H256::from_low_u64_be(4),
            get_abi_encoded_string(symbol.as_str()),
        ),
    ];

    Ok(DeployContractInfo {
        address,
        bytecode: Bytes::from(bytecode),
        storage,
    })
}

pub fn bridge_dst20(
    backend: &EVMBackend,
    contract: H160,
    from: H160,
    amount: U256,
    out: bool,
) -> Result<DST20BridgeInfo> {
    // check if code of address matches DST20 bytecode
    let account = backend
        .get_account(&contract)
        .ok_or_else(|| format_err!("DST20 token address is not a contract"))?;

    let Contract { codehash, .. } = get_dst20_contract();
    if account.code_hash != codehash {
        return Err(format_err!("DST20 token code is not valid").into());
    }

    let storage_index = get_address_storage_index(from);
    let balance = backend.get_contract_storage(contract, storage_index.as_bytes())?;

    let total_supply_index = H256::from_low_u64_be(2);
    let total_supply = backend.get_contract_storage(contract, total_supply_index.as_bytes())?;

    let (new_balance, new_total_supply) = if out {
        (
            balance.checked_sub(amount),
            total_supply.checked_sub(amount),
        )
    } else {
        (
            balance.checked_add(amount),
            total_supply.checked_add(amount),
        )
    };

    let new_balance = new_balance.ok_or_else(|| format_err!("Balance overflow/underflow"))?;
    let new_total_supply =
        new_total_supply.ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    Ok(DST20BridgeInfo {
        address: contract,
        storage: vec![
            (storage_index, u256_to_h256(new_balance)),
            (total_supply_index, u256_to_h256(new_total_supply)),
        ],
    })
}
