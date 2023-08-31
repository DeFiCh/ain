use std::str::FromStr;

use anyhow::format_err;
use primitive_types::{H160, H256, U256};
use sha3::{Digest, Keccak256};
use sp_core::{Blake2Hasher, Hasher};

pub type Result<T> = std::result::Result<T, anyhow::Error>;

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

pub fn get_bytecode(input: &str) -> Result<Vec<u8>> {
    let bytecode_json: serde_json::Value = serde_json::from_str(input)?;
    let bytecode_raw = bytecode_json["object"]
        .as_str()
        .ok_or_else(|| format_err!("Bytecode object not available".to_string()))?;

    hex::decode(&bytecode_raw[2..]).map_err(|e| format_err!(e.to_string()))
}

pub fn get_dst20_input() -> Vec<u8> {
    DST20_INPUT.clone()
}

pub fn dst20_address_from_token_id(token_id: u64) -> Result<H160> {
    let number_str = format!("{:x}", token_id);
    let padded_number_str = format!("{number_str:0>38}");
    let final_str = format!("ff{padded_number_str}");

    Ok(H160::from_str(&final_str)?)
}

    #[derive(Clone)]
    pub struct Contract {
    pub codehash: H256,
    pub bytecode: Vec<u8>,
    pub fixed_address: Option<H160>,
}

lazy_static::lazy_static! {
    pub static ref DST20_INPUT: Vec<u8> = get_bytecode(include_str!("../dst20/input.json")).unwrap();

    pub static ref INTRINSIC_CONTRACT: Contract = {
        let bytecode = get_bytecode(include_str!(concat!(
                env!("CARGO_TARGET_DIR"),
                "/ain_contracts/dfi_intrinsics/bytecode.json"
        ))).unwrap();

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            bytecode,
            fixed_address: Some(H160([
                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                0x3, 0x1,
            ])),
        }
    };

    pub static ref TRANSFERDOMAIN_CONTRACT: Contract = {
        let bytecode = get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/transfer_domain/bytecode.json"
        ))).unwrap();

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            bytecode,
            fixed_address: Some(H160([
                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                0x3, 0x2,
            ])),
        }
    };

    pub static ref DST20_CONTRACT: Contract = {
        let bytecode = get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/dst20/bytecode.json"
        ))).unwrap();

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            bytecode,
            fixed_address: None,
        }
    };

    pub static ref RESERVED_CONTRACT: Contract = {
        let bytecode = get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/system_reserved/bytecode.json"
        ))).unwrap();

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            bytecode,
            fixed_address: None,
        }
    };
}

pub fn get_intrinsic_contract() -> Contract {
    INTRINSIC_CONTRACT.clone()
}

pub fn get_transferdomain_contract() -> Contract {
    TRANSFERDOMAIN_CONTRACT.clone()
}

pub fn get_dst20_contract() -> Contract {
    DST20_CONTRACT.clone()
}

pub fn get_reserved_contract() -> Contract {
    RESERVED_CONTRACT.clone()
}
