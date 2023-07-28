use std::error::Error;

#[cfg(not(test))]
mod bridge;

#[cfg(not(test))]
use bridge::ffi;

#[cfg(test)]
#[allow(non_snake_case)]
mod ffi {
    const UNIMPL_MSG: &str = "This cannot be used on a test path";
    pub fn getChainId() -> u64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn isMining() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn publishEthTransaction(_data: Vec<u8>) -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getAccounts() -> Vec<String> {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getDatadir() -> String {
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
    pub fn getPoolTransactions() -> Vec<String> {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getNativeTxSize(_data: Vec<u8>) -> u64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getMinRelayTxFee() -> u64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEthPrivKey(_key_id: [u8; 20]) -> [u8; 32] {
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
    pub fn pastChangiIntermediateHeight2() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn pastChangiIntermediateHeight3() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn pastChangiIntermediateHeight4() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn pastChangiIntermediateHeight5() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }

    pub fn CppLogPrintf(_message: String) {
        // Intentionally left empty, so it can be used from everywhere.
        // Just the logs are skipped.
    }
}

pub fn get_chain_id() -> Result<u64, Box<dyn Error>> {
    let chain_id = ffi::getChainId();
    Ok(chain_id)
}

pub fn is_mining() -> Result<bool, Box<dyn Error>> {
    let is_mining = ffi::isMining();
    Ok(is_mining)
}

pub fn publish_eth_transaction(data: Vec<u8>) -> Result<String, Box<dyn Error>> {
    let publish = ffi::publishEthTransaction(data);
    Ok(publish)
}

pub fn get_accounts() -> Result<Vec<String>, Box<dyn Error>> {
    let accounts = ffi::getAccounts();
    Ok(accounts)
}

pub fn get_datadir() -> String {
    ffi::getDatadir()
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

pub fn get_pool_transactions() -> Result<Vec<String>, Box<dyn Error>> {
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

pub fn get_eth_priv_key(key_id: [u8; 20]) -> Result<[u8; 32], Box<dyn Error>> {
    let eth_key = ffi::getEthPrivKey(key_id);
    Ok(eth_key)
}

pub fn get_state_input_json() -> Option<String> {
    let json_path = ffi::getStateInputJSON();
    if json_path.is_empty() {
        None
    } else {
        Some(json_path)
    }
}

pub fn get_sync_status() -> Result<(i32, i32), Box<dyn Error>> {
    let current_block = ffi::getCurrentHeight();
    let highest_block = ffi::getHighestBlock();
    Ok((current_block, highest_block))
}

pub fn past_changi_intermediate_height_2_height() -> bool {
    ffi::pastChangiIntermediateHeight2()
}

pub fn past_changi_intermediate_height_3_height() -> bool {
    ffi::pastChangiIntermediateHeight3()
}

pub fn past_changi_intermediate_height_4_height() -> bool {
    ffi::pastChangiIntermediateHeight4()
}

pub fn past_changi_intermediate_height_5_height() -> bool {
    ffi::pastChangiIntermediateHeight5()
}

pub fn log_print(message: &str) {
    // TODO: Switch to u8 to avoid intermediate string conversions
    ffi::CppLogPrintf(message.to_owned());
}

#[cfg(test)]
mod tests {}
