use std::str::FromStr;

use anyhow::format_err;
use ethereum_types::{H160, H256, U256};
use sp_core::{Blake2Hasher, Hasher};

pub type Result<T> = std::result::Result<T, anyhow::Error>;

pub const DST20_ADDR_PREFIX_BYTE: u8 = 0xff;
const INTRINSICS_ADDR_PREFIX_BYTE: u8 = 0xdf;

// Impl slots used for proxies: 0x360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbc
pub const IMPLEMENTATION_SLOT: H256 = H256([
    0x36, 0x08, 0x94, 0xa1, 0x3b, 0xa1, 0xa3, 0x21, 0x06, 0x67, 0xc8, 0x28, 0x49, 0x2d, 0xb9, 0x8d,
    0xca, 0x3e, 0x20, 0x76, 0xcc, 0x37, 0x35, 0xa9, 0x20, 0xa3, 0xca, 0x50, 0x5d, 0x38, 0x2b, 0xbc,
]);

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
    ($byte0:tt, $byte19:tt) => {{
        let mut v = [0; 20];
        v[0] = $byte0;
        v[19] = $byte19;
        v
    }};
}

fn get_bytecode(input: &str) -> Result<Vec<u8>> {
    let bytecode_json: serde_json::Value = serde_json::from_str(input)?;
    let bytecode_raw = bytecode_json["object"]
        .as_str()
        .ok_or_else(|| format_err!("Bytecode object not available".to_string()))?;

    hex::decode(&bytecode_raw[2..]).map_err(|e| format_err!(e.to_string()))
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
    pub static ref DFI_RESERVED_CONTRACT: Contract = {
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

    pub static ref DFI_INSTRINICS_REGISTRY_CONTRACT : FixedContract = {
        let bytecode = solc_artifact_bytecode_str!("dfi_intrinsics_registry", "deployed_bytecode.json");
        let input = solc_artifact_bytecode_str!(
            "dfi_intrinsics_registry",
            "bytecode.json"
        );

        FixedContract {
            contract: Contract {
                codehash: Blake2Hasher::hash(&bytecode),
                runtime_bytecode: bytecode,
                init_bytecode: input,
            },
            fixed_address: H160(slice_20b!(INTRINSICS_ADDR_PREFIX_BYTE, 0x0)),
        }
    };

    pub static ref DFI_INTRINSICS_V1_CONTRACT: FixedContract = {
        let bytecode = solc_artifact_bytecode_str!("dfi_intrinsics_v1", "deployed_bytecode.json");
        let input = solc_artifact_bytecode_str!(
            "dfi_intrinsics_v1",
            "bytecode.json"
        );

        FixedContract {
            contract: Contract {
                codehash: Blake2Hasher::hash(&bytecode),
                runtime_bytecode: bytecode,
                init_bytecode: input,
            },
            fixed_address: H160(slice_20b!(INTRINSICS_ADDR_PREFIX_BYTE, 0x2)),
        }
    };

    pub static ref TRANSFERDOMAIN_CONTRACT : FixedContract = {
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

    pub static ref TRANSFERDOMAIN_V1_CONTRACT: FixedContract = {
        let bytecode = solc_artifact_bytecode_str!("transfer_domain_v1", "deployed_bytecode.json");
        let input = solc_artifact_bytecode_str!(
            "transfer_domain_v1",
            "bytecode.json"
        );

        FixedContract {
            contract: Contract {
                codehash: Blake2Hasher::hash(&bytecode),
                runtime_bytecode: bytecode,
                init_bytecode: input,
            },
            fixed_address: H160(slice_20b!(INTRINSICS_ADDR_PREFIX_BYTE, 0x3)),
        }
    };

    pub static ref DST20_CONTRACT: Contract = {
        let bytecode = solc_artifact_bytecode_str!(
            "dst20",
            "deployed_bytecode.json"
        );

        Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            runtime_bytecode: bytecode,
            init_bytecode: Vec::new(),
        }
    };

    pub static ref DST20_V1_CONTRACT: FixedContract = {
        let bytecode = solc_artifact_bytecode_str!(
            "dst20_v1", "deployed_bytecode.json"
        );
        let input = solc_artifact_bytecode_str!(
            "dst20_v1", "bytecode.json"
        );

        FixedContract {
            contract: Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            runtime_bytecode: bytecode,
            init_bytecode: input,
            },
            fixed_address: H160(slice_20b!(INTRINSICS_ADDR_PREFIX_BYTE, 0x4))
        }
    };

    pub static ref DST20_V2_CONTRACT: FixedContract = {
        let bytecode = solc_artifact_bytecode_str!(
            "dst20_v2", "deployed_bytecode.json"
        );
        let input = solc_artifact_bytecode_str!(
            "dst20_v2", "bytecode.json"
        );

        FixedContract {
            contract: Contract {
            codehash: Blake2Hasher::hash(&bytecode),
            runtime_bytecode: bytecode,
            init_bytecode: input,
            },
            fixed_address: H160(slice_20b!(INTRINSICS_ADDR_PREFIX_BYTE, 0x5))
        }
    };
}

pub fn get_split_tokens_function() -> ethabi::Function {
    #[allow(deprecated)] // constant field is deprecated since Solidity 0.5.0
    ethabi::Function {
        name: String::from("migrateTokensOnAddress"),
        inputs: vec![
            ethabi::Param {
                name: String::from("sender"),
                kind: ethabi::ParamType::Address,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("tokenContract"),
                kind: ethabi::ParamType::Address,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("amount"),
                kind: ethabi::ParamType::Uint(256),
                internal_type: None,
            },
        ],
        outputs: vec![],
        constant: None,
        state_mutability: ethabi::StateMutability::NonPayable,
    }
}

pub struct TokenSplitParams {
    pub sender: H160,
    pub token_contract: H160,
    pub amount: U256,
}

pub fn validate_split_tokens_input(input: &[u8]) -> Result<TokenSplitParams> {
    let function = get_split_tokens_function();
    let token_inputs = function.decode_input(input)?;

    let Some(ethabi::Token::Address(sender)) = token_inputs.first() else {
        return Err(format_err!("invalid from address input in evm tx"));
    };
    let Some(ethabi::Token::Address(token_contract)) = token_inputs.get(1).cloned() else {
        return Err(format_err!("invalid from address input in evm tx"));
    };
    let Some(ethabi::Token::Uint(amount)) = token_inputs.get(2).cloned() else {
        return Err(format_err!("invalid value input in evm tx"));
    };

    Ok(TokenSplitParams {
        sender: *sender,
        token_contract,
        amount,
    })
}

pub fn get_transferdomain_native_transfer_function() -> ethabi::Function {
    #[allow(deprecated)] // constant field is deprecated since Solidity 0.5.0
    ethabi::Function {
        name: String::from("transfer"),
        inputs: vec![
            ethabi::Param {
                name: String::from("from"),
                kind: ethabi::ParamType::Address,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("to"),
                kind: ethabi::ParamType::Address,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("amount"),
                kind: ethabi::ParamType::Uint(256),
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("vmAddress"),
                kind: ethabi::ParamType::String,
                internal_type: None,
            },
        ],
        outputs: vec![],
        constant: None,
        state_mutability: ethabi::StateMutability::NonPayable,
    }
}

pub fn get_transferdomain_dst20_transfer_function() -> ethabi::Function {
    #[allow(deprecated)] // constant field is deprecated since Solidity 0.5.0
    ethabi::Function {
        name: String::from("transferDST20"),
        inputs: vec![
            ethabi::Param {
                name: String::from("contractAddress"),
                kind: ethabi::ParamType::Address,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("from"),
                kind: ethabi::ParamType::Address,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("to"),
                kind: ethabi::ParamType::Address,
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("amount"),
                kind: ethabi::ParamType::Uint(256),
                internal_type: None,
            },
            ethabi::Param {
                name: String::from("vmAddress"),
                kind: ethabi::ParamType::String,
                internal_type: None,
            },
        ],
        outputs: vec![],
        constant: None,
        state_mutability: ethabi::StateMutability::NonPayable,
    }
}

pub fn get_dfi_reserved_contract() -> Contract {
    DFI_RESERVED_CONTRACT.clone()
}

pub fn get_dfi_instrinics_registry_contract() -> FixedContract {
    DFI_INSTRINICS_REGISTRY_CONTRACT.clone()
}

pub fn get_dfi_intrinsics_v1_contract() -> FixedContract {
    DFI_INTRINSICS_V1_CONTRACT.clone()
}

pub fn get_transfer_domain_contract() -> FixedContract {
    TRANSFERDOMAIN_CONTRACT.clone()
}

pub fn get_transfer_domain_v1_contract() -> FixedContract {
    TRANSFERDOMAIN_V1_CONTRACT.clone()
}

pub fn get_dst20_contract() -> Contract {
    DST20_CONTRACT.clone()
}

pub fn get_dst20_v1_contract() -> FixedContract {
    DST20_V1_CONTRACT.clone()
}

pub fn get_dst20_v2_contract() -> FixedContract {
    DST20_V2_CONTRACT.clone()
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_addr_gen() {
        let items: Vec<(Result<H160>, &str)> = vec![
            (
                generate_intrinsic_addr(DST20_ADDR_PREFIX_BYTE, 0x0),
                "ff00000000000000000000000000000000000000",
            ),
            (
                generate_intrinsic_addr(DST20_ADDR_PREFIX_BYTE, 0x1),
                "ff00000000000000000000000000000000000001",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x0),
                "df00000000000000000000000000000000000000",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x1),
                "df00000000000000000000000000000000000001",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x2),
                "df00000000000000000000000000000000000002",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xff),
                "df000000000000000000000000000000000000ff",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xfe),
                "df000000000000000000000000000000000000fe",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x1fffe),
                "df0000000000000000000000000000000001fffe",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xffff),
                "df0000000000000000000000000000000000ffff",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xffff_ffff_ffff_ffff),
                "df0000000000000000000000ffffffffffffffff",
            ),
            (
                generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0xefff_ffff_ffff_ffff),
                "df0000000000000000000000efffffffffffffff",
            ),
            // Should not compile
            // (generate_intrinsic_addr(INTRINSICS_ADDR_PREFIX_BYTE, 0x1ffff_ffff_ffff_ffff), "df0000000000000000000001ffffffffffffffff"),
        ];

        for x in items {
            assert_eq!(x.0.unwrap(), H160::from_str(x.1).unwrap());
        }
    }
}
