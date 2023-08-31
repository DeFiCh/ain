use std::str::FromStr;

use anyhow::format_err;
use primitive_types::{H160, H256, U256};
use sp_core::{Blake2Hasher, Hasher};

pub type Result<T> = std::result::Result<T, anyhow::Error>;

fn get_bytecode(input: &str) -> Result<Vec<u8>> {
    let bytecode_json: serde_json::Value = serde_json::from_str(input)?;
    let bytecode_raw = bytecode_json["object"]
        .as_str()
        .ok_or_else(|| format_err!("Bytecode object not available".to_string()))?;

    hex::decode(&bytecode_raw[2..]).map_err(|e| format_err!(e.to_string()))
}

pub fn dst20_address_from_token_id(token_id: u64) -> Result<H160> {
    let number_str = format!("{:x}", token_id);
    let padded_number_str = format!("{number_str:0>38}");
    let final_str = format!("ff{padded_number_str}");

    Ok(H160::from_str(&final_str)?)
}

pub struct IntrinsicContract;
pub struct TransferDomainContract;
pub struct DST20Contract;
pub struct ReservedContract;

pub trait Contract {
    fn bytecode() -> Result<Vec<u8>>;
    fn codehash() -> Result<H256> {
        let bytecode = Self::bytecode()?;
        Ok(Blake2Hasher::hash(&bytecode))
    }
}

impl Contract for IntrinsicContract {
    fn bytecode() -> Result<Vec<u8>> {
        get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/dfi_intrinsics/bytecode.json"
        )))
    }
}

impl Contract for TransferDomainContract {
    fn bytecode() -> Result<Vec<u8>> {
        get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/transfer_domain/bytecode.json"
        )))
    }
}

impl Contract for DST20Contract {
    fn bytecode() -> Result<Vec<u8>> {
        get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/dst20/bytecode.json"
        )))
    }
}

impl Contract for ReservedContract {
    fn bytecode() -> Result<Vec<u8>> {
        get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/system_reserved/bytecode.json"
        )))
    }
}

pub trait FixedContract {
    const ADDRESS: H160;
}

impl FixedContract for IntrinsicContract {
    const ADDRESS: H160 = H160([
        0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
        0x3, 0x1,
    ]);
}

impl FixedContract for TransferDomainContract {
    const ADDRESS: H160 = H160([
        0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
        0x3, 0x2,
    ]);
}

pub trait InputContract {
    fn input() -> Result<Vec<u8>>;
}

impl InputContract for DST20Contract {
    fn input() -> Result<Vec<u8>> {
        get_bytecode(include_str!("../dst20/input.json"))
    }
}

impl InputContract for TransferDomainContract {
    fn input() -> Result<Vec<u8>> {
        get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/transfer_domain/bytecode.json"
        )))
    }
}
