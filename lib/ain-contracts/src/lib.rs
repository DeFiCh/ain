use std::str::FromStr;

use anyhow::format_err;
use primitive_types::{H160, H256};
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

#[derive(Clone)]
pub struct Contract {
    pub codehash: H256,
    pub bytecode: Vec<u8>,
    pub input: Vec<u8>,
    pub fixed_address: Option<H160>,
}

lazy_static::lazy_static! {
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
            input: Vec::new()
        }
    };

    pub static ref TRANSFERDOMAIN_CONTRACT: Contract = {
        let bytecode = get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/transfer_domain/bytecode.json"
        ))).unwrap();
        let input = get_bytecode(include_str!(
            "../transfer_domain/input.json"
        )).unwrap();

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            bytecode,
            fixed_address: Some(H160([
                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                0x3, 0x2,
            ])),
            input
        }
    };

    pub static ref DST20_CONTRACT: Contract = {
        let bytecode = get_bytecode(include_str!(concat!(
            env!("CARGO_TARGET_DIR"),
            "/ain_contracts/dst20/bytecode.json"
        ))).unwrap();
        let input = get_bytecode(include_str!(
            "../dst20/input.json"
        )).unwrap();

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            bytecode,
            fixed_address: None,
            input
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
            input: Vec::new()
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
