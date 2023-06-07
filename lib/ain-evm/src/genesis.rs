use ethereum_types::{H160, H256, H64, U256};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Serialize, Deserialize)]
struct Config {
    chain_id: u32,
    homestead_block: u32,
    eip150_block: u32,
    eip150_hash: String,
    eip155_block: u32,
    eip158_block: u32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Alloc {
    pub balance: U256,
    pub code: Option<Vec<u8>>,
    pub storage: Option<HashMap<H256, H256>>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GenesisData {
    // config: Config,
    coinbase: String,
    difficulty: String,
    extra_data: String,
    gas_limit: U256,
    nonce: H64,
    timestamp: U256,
    pub alloc: HashMap<H160, Alloc>,
}
