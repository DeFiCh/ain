use primitive_types::{H160, H256, U256};

pub fn format_h256(hash: H256) -> String {
    format!("{hash:#x}")
}

pub fn format_address(hash: H160) -> String {
    format!("{hash:#x}")
}

pub fn format_u256(number: U256) -> String {
    format!("{number:#x}")
}

pub fn format_bytes(bytes: Vec<u8>) -> String {
    format!("0x{}", String::from_utf8(bytes).unwrap())
}