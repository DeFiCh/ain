use std::error::Error;

#[cfg(not(test))]
mod bridge;

#[cfg(not(test))]
use bridge::ffi;

#[cfg(test)]
#[allow(non_snake_case)]
mod ffi {
    pub struct Attributes {
        pub block_gas_target: u64,
        pub block_gas_limit: u64,
        pub finality_count: u64,
    }

    pub struct DST20Token {
        pub id: u64,
        pub name: String,
        pub symbol: String,
    }

    pub struct TransactionData {
        pub tx_type: u8,
        pub data: String,
        pub direction: u8,
    }

    const UNIMPL_MSG: &str = "This cannot be used on a test path";
    pub fn getChainId() -> u64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn isMining() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn publishEvmTransaction(_data: Vec<u8>) -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getAccounts() -> Vec<String> {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getDatadir() -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEvmMaxConnections() -> u32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getNetwork() -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getDifficulty(_block_hash: [u8; 32]) -> u32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getChainWork(_block_hash: [u8; 32]) -> [u8; 32] {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getPoolTransactions() -> Vec<TransactionData> {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getNativeTxSize(_data: Vec<u8>) -> u64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getMinRelayTxFee() -> u64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEvmPrivKey(_key: String) -> [u8; 32] {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getStateInputJSON() -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getHighestBlock() -> i32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getCurrentHeight() -> i32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getAttributeDefaults() -> Attributes {
        unimplemented!("{}", UNIMPL_MSG)
    }

    pub fn CppLogPrintf(_message: String) {
        // Intentionally left empty, so it can be used from everywhere.
        // Just the logs are skipped.
    }

    pub fn getDST20Tokens(_mnview_ptr: usize) -> Vec<DST20Token> {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getClientVersion() -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getNumCores() -> i32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getCORSAllowedOrigin() -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getNumConnections() -> i32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
}

pub use ffi::Attributes;

pub fn get_chain_id() -> Result<u64, Box<dyn Error>> {
    let chain_id = ffi::getChainId();
    Ok(chain_id)
}

pub fn get_client_version() -> String {
    ffi::getClientVersion()
}

pub fn is_mining() -> Result<bool, Box<dyn Error>> {
    let is_mining = ffi::isMining();
    Ok(is_mining)
}

pub fn publish_evm_transaction(data: Vec<u8>) -> Result<String, Box<dyn Error>> {
    let publish = ffi::publishEvmTransaction(data);
    Ok(publish)
}

pub fn get_accounts() -> Result<Vec<String>, Box<dyn Error>> {
    let accounts = ffi::getAccounts();
    Ok(accounts)
}

pub fn get_datadir() -> String {
    ffi::getDatadir()
}

pub fn get_max_connections() -> u32 {
    ffi::getEvmMaxConnections()
}

pub fn get_network() -> String {
    ffi::getNetwork()
}

pub fn get_difficulty(block_hash: [u8; 32]) -> Result<u32, Box<dyn Error>> {
    let bits = ffi::getDifficulty(block_hash);
    Ok(bits)
}

pub fn get_chainwork(block_hash: [u8; 32]) -> Result<[u8; 32], Box<dyn Error>> {
    let chainwork = ffi::getChainWork(block_hash);
    Ok(chainwork)
}

pub fn get_pool_transactions() -> Result<Vec<ffi::TransactionData>, Box<dyn Error>> {
    let transactions = ffi::getPoolTransactions();
    Ok(transactions)
}

pub fn get_native_tx_size(data: Vec<u8>) -> Result<u64, Box<dyn Error>> {
    let tx_size = ffi::getNativeTxSize(data);
    Ok(tx_size)
}

pub fn get_min_relay_tx_fee() -> Result<u64, Box<dyn Error>> {
    let tx_fee = ffi::getMinRelayTxFee();
    Ok(tx_fee)
}

pub fn get_evm_priv_key(key: String) -> Result<[u8; 32], Box<dyn Error>> {
    let evm_key = ffi::getEvmPrivKey(key);
    Ok(evm_key)
}

pub fn get_state_input_json() -> Option<String> {
    let json_path = ffi::getStateInputJSON();
    if json_path.is_empty() {
        None
    } else {
        Some(json_path)
    }
}

/// Returns current DVM block height and highest DVM block header seen
pub fn get_sync_status() -> Result<(i32, i32), Box<dyn Error>> {
    let current_block = ffi::getCurrentHeight();
    let highest_block = ffi::getHighestBlock();
    Ok((current_block, highest_block))
}

pub fn get_attribute_defaults() -> ffi::Attributes {
    ffi::getAttributeDefaults()
}

pub fn log_print(message: &str) {
    // TODO: Switch to u8 to avoid intermediate string conversions
    ffi::CppLogPrintf(message.to_owned());
}

pub fn get_dst20_tokens(mnview_ptr: usize) -> Vec<ffi::DST20Token> {
    ffi::getDST20Tokens(mnview_ptr)
}

pub fn get_num_cores() -> i32 {
    ffi::getNumCores()
}

pub fn get_cors_allowed_origin() -> String {
    ffi::getCORSAllowedOrigin()
}

pub fn get_num_connections() -> i32 {
    ffi::getNumConnections()
}

pub fn get_evm_cache_size_limit() -> usize {
    ffi::getEvmCacheSizeLimit()
}

#[cfg(test)]
mod tests {}
