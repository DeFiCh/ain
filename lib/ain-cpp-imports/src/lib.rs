use std::error::Error;

#[cfg(not(any(test, bench, example, doc)))]
mod bridge;

#[cfg(not(any(test, bench, example, doc)))]
use bridge::ffi;

#[cfg(any(test, bench, example, doc))]
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

pub fn get_datadir() -> Result<String, Box<dyn Error>> {
    let datadir = ffi::getDatadir();
    Ok(datadir)
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

#[cfg(test)]
mod tests {}
