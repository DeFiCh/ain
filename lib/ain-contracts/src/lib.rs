use anyhow::anyhow;
use lazy_static::lazy_static;
use primitive_types::{H160, H256, U256};
use sha3::{Digest, Keccak256};
use std::collections::HashMap;
use std::error::Error;
use std::str::FromStr;

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

pub fn get_bytecode(input: &str) -> Result<Vec<u8>, Box<dyn Error>> {
    let bytecode_json: serde_json::Value = serde_json::from_str(input)?;
    let bytecode_raw = bytecode_json["object"]
        .as_str()
        .ok_or_else(|| anyhow!("Bytecode object not available".to_string()))?;

    Ok(hex::decode(&bytecode_raw[2..]).map_err(|e| anyhow!(e.to_string()))?)
}

pub fn get_counter_bytecode() -> Result<Vec<u8>, Box<dyn Error>> {
    get_bytecode(include_str!("../counter_contract/output/bytecode.json"))
}

pub fn get_dst20_bytecode() -> Result<Vec<u8>, Box<dyn Error>> {
    get_bytecode(include_str!("../dst20/output/bytecode.json"))
}

pub fn dst20_address_from_token_id(token_id: &str) -> Result<H160, Box<dyn Error>> {
    let number_str = format!("{:x}", token_id.parse::<u64>()?);
    let padded_number_str = format!("{:0>38}", number_str);
    let final_str = format!("ff{}", padded_number_str);

    Ok(H160::from_str(&final_str)?)
}

#[derive(Debug, PartialEq, Eq, Hash)]
pub enum Contracts {
    CounterContract,
}

lazy_static! {
    pub static ref CONTRACT_ADDRESSES: HashMap<Contracts, H160> = HashMap::from([(
        Contracts::CounterContract,
        H160::from_str("0x0000000000000000000000000000000000000301").unwrap()
    ),]);
}
