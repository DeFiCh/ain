use std::str::FromStr;

use anyhow::format_err;
use ethereum_types::{H160, H256};
use sp_core::{Blake2Hasher, Hasher};

pub type Result<T> = std::result::Result<T, anyhow::Error>;

pub const DST20_ADDR_PREFIX_BYTE: u8 = 0xff;
pub const INTRINSICS_ADDR_PREFIX_BYTE: u8 = 0xdf;

macro_rules! solc_artifact_path {
    ($project_name:literal, $artifact:literal) => {
        concat!(
            env!("CARGO_TARGET_DIR"),
            "/sol_artifacts/",
            $project_name,
            "/",
            $artifact
        )
    };
}

macro_rules! solc_artifact_content_str {
    ($project_name:literal, $artifact:literal) => {
        include_str!(solc_artifact_path!($project_name, $artifact))
    };
}

macro_rules! solc_artifact_bytecode_str {
    ($project_name:literal, $artifact:literal) => {
        get_bytecode(solc_artifact_content_str!($project_name, $artifact)).unwrap()
    };
}

macro_rules! slice_20b {
    ($first_byte:tt, $last_byte:tt) => {
        [
            $first_byte,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            0x0,
            $last_byte,
        ]
    };
}

fn get_bytecode(input: &str) -> Result<Vec<u8>> {
    let bytecode_json: serde_json::Value = serde_json::from_str(input)?;
    let bytecode_raw = bytecode_json["object"]
        .as_str()
        .ok_or_else(|| format_err!("Bytecode object not available".to_string()))?;

    hex::decode(&bytecode_raw[2..]).map_err(|e| format_err!(e.to_string()))
}

pub fn get_dst20_deploy_input(init_bytecode: Vec<u8>, name: &str, symbol: &str) -> Result<Vec<u8>> {
    let name = ethabi::Token::String(name.to_string());
    let symbol = ethabi::Token::String(symbol.to_string());

    let constructor = ethabi::Constructor {
        inputs: vec![
            ethabi::Param {
                name: String::from("name"),
                kind: ethabi::ParamType::String,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("symbol"),
                kind: ethabi::ParamType::String,
                internal_type: None,
            },
        ],
    };

    constructor
        .encode_input(init_bytecode, &[name, symbol])
        .map_err(|e| format_err!(e))
}

pub fn generate_intrinsic_addr(prefix_byte: u8, suffix_num: u64) -> Result<H160> {
    let s = format!("{prefix_byte:x}{suffix_num:0>38x}");

    Ok(H160::from_str(&s)?)
}

pub fn dst20_address_from_token_id(token_id: u64) -> Result<H160> {
    generate_intrinsic_addr(DST20_ADDR_PREFIX_BYTE, token_id)
}

pub fn intrinsics_address_from_id(id: u64) -> Result<H160> {
    generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, id)
}

#[derive(Clone)]
pub struct Contract {
    pub codehash: H256,
    pub runtime_bytecode: Vec<u8>,
    pub init_bytecode: Vec<u8>,
}

#[derive(Clone)]
pub struct FixedContract {
    pub contract: Contract,
    pub fixed_address: H160,
}

lazy_static::lazy_static! {
    pub static ref INTRINSIC_CONTRACT: FixedContract = {
        let bytecode = solc_artifact_bytecode_str!("dfi_intrinsics", "deployed_bytecode.json");

        FixedContract {
            contract: Contract {
                codehash: Blake2Hasher::hash(&bytecode),
                runtime_bytecode: bytecode,
                init_bytecode: Vec::new(),
            },
            fixed_address: H160(slice_20b!(INTRINSICS_ADDR_PREFIX_BYTE, 0x0)),
        }
    };

    pub static ref TRANSFERDOMAIN_CONTRACT: FixedContract = {
        let bytecode = solc_artifact_bytecode_str!("transfer_domain", "deployed_bytecode.json");
        let input = solc_artifact_bytecode_str!(
            "transfer_domain",
            "bytecode.json"
        );

        FixedContract {
            contract: Contract {
                codehash: Blake2Hasher::hash(&bytecode),
                runtime_bytecode: bytecode,
                init_bytecode: input,
            },
            fixed_address: H160(slice_20b!(INTRINSICS_ADDR_PREFIX_BYTE, 0x1)),
        }
    };

    pub static ref DST20_CONTRACT: Contract = {
        let bytecode = solc_artifact_bytecode_str!(
            "dst20", "deployed_bytecode.json"
        );
        let input = solc_artifact_bytecode_str!(
            "dst20", "bytecode.json"
        );

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            runtime_bytecode: bytecode,
            init_bytecode: input,
        }
    };

    pub static ref RESERVED_CONTRACT: Contract = {
        let bytecode = solc_artifact_bytecode_str!(
            "dfi_reserved",
            "deployed_bytecode.json"
        );

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            runtime_bytecode: bytecode,
            init_bytecode: Vec::new(),
        }
    };
}

pub fn get_intrinsic_contract() -> FixedContract {
    INTRINSIC_CONTRACT.clone()
}

pub fn get_transferdomain_contract() -> FixedContract {
    TRANSFERDOMAIN_CONTRACT.clone()
}

pub fn get_dst20_contract() -> Contract {
    DST20_CONTRACT.clone()
}

pub fn get_reserved_contract() -> Contract {
    RESERVED_CONTRACT.clone()
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_addr_gen() {
        let items: Vec<(Result<H160>, &str)> = vec![
            (generate_intrinsic_addr(DST20_ADDR_PREFIX_BYTE, 0x0), "ff00000000000000000000000000000000000000"),
            (generate_intrinsic_addr(DST20_ADDR_PREFIX_BYTE, 0x1), "ff00000000000000000000000000000000000001"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x0), "df00000000000000000000000000000000000000"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x1), "df00000000000000000000000000000000000001"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x2), "df00000000000000000000000000000000000002"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xff), "df000000000000000000000000000000000000ff"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xfe), "df000000000000000000000000000000000000fe"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x1fffe), "df0000000000000000000000000000000001fffe"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xffff), "df0000000000000000000000000000000000ffff"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xffff_ffff_ffff_ffff), "df0000000000000000000000ffffffffffffffff"),
            (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xefff_ffff_ffff_ffff), "df0000000000000000000000efffffffffffffff"),
            // Should not compile
            // (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x1ffff_ffff_ffff_ffff), "df0000000000000000000001ffffffffffffffff"),
        ];

        for x in items {
            assert_eq!(x.0.unwrap(), H160::from_str(x.1).unwrap());
        }
    }
}
