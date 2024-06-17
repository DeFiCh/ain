use std::error::Error;

#[cfg(not(test))]
mod bridge;

#[cfg(not(test))]
use bridge::ffi;

#[cfg(test)]
#[allow(non_snake_case)]
mod ffi {
    pub struct Attributes {
        pub block_gas_target_factor: u64,
        pub block_gas_limit: u64,
        pub finality_count: u64,
        pub rbf_fee_increment: u64,
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
        pub entry_time: i64,
    }

    pub enum SystemTxType {
        EVMTx,
        TransferDomainIn,
        TransferDomainOut,
        DST20BridgeIn,
        DST20BridgeOut,
        DeployContract,
        UpdateContractName,
    }

    pub struct SystemTxData {
        pub tx_type: SystemTxType,
        pub token: DST20Token,
    }

    pub struct TokenAmount {
        pub id: u32,
        pub amount: u64,
    }

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
    pub fn getEthMaxConnections() -> u32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn printEVMPortUsage(_port_type: u8, _port_number: u16) {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEthMaxResponseByteSize() -> u32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEthTracingMaxMemoryUsageBytes() -> u32 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getSuggestedPriorityFeePercentile() -> i64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEstimateGasErrorRatio() -> u64 {
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
    pub fn getEthPrivKey(_key: [u8; 20]) -> [u8; 32] {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getStateInputJSON() -> String {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEthSyncStatus() -> [i64; 2] {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getAttributeValues(_mnview_ptr: usize) -> Attributes {
        unimplemented!("{}", UNIMPL_MSG)
    }

    pub fn CppLogPrintf(_message: String) {
        // Intentionally left empty, so it can be used from everywhere.
        // Just the logs are skipped.
    }

    #[allow(clippy::ptr_arg)]
    pub fn getDST20Tokens(_mnview_ptr: usize, _tokens: &mut Vec<DST20Token>) -> bool {
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
    pub fn getEccLruCacheCount() -> usize {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEvmValidationLruCacheCount() -> usize {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEvmNotificationChannelBufferSize() -> usize {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn isEthDebugRPCEnabled() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn isEthDebugTraceRPCEnabled() -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getEVMSystemTxsFromBlock(_block_hash: [u8; 32]) -> Vec<SystemTxData> {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn getDF23Height() -> u64 {
        unimplemented!("{}", UNIMPL_MSG)
    }
    pub fn migrateTokensFromEVM(
        _mnview_ptr: usize,
        _old_amount: TokenAmount,
        _new_amount: &mut TokenAmount,
    ) -> bool {
        unimplemented!("{}", UNIMPL_MSG)
    }
}

pub use ffi::Attributes;
pub use ffi::SystemTxData;
pub use ffi::SystemTxType;
pub use ffi::TokenAmount;

/// Returns the chain ID of the current network.
pub fn get_chain_id() -> Result<u64, Box<dyn Error>> {
    let chain_id = ffi::getChainId();
    Ok(chain_id)
}

/// Retrieves the client version string.
pub fn get_client_version() -> String {
    ffi::getClientVersion()
}

/// Determines if mining is currently enabled with the defid -gen flag.
pub fn is_mining() -> Result<bool, Box<dyn Error>> {
    let is_mining = ffi::isMining();
    Ok(is_mining)
}

/// Publishes an EVM transaction with the given raw data.
pub fn publish_eth_transaction(data: Vec<u8>) -> Result<String, Box<dyn Error>> {
    let publish = ffi::publishEthTransaction(data);
    Ok(publish)
}

/// Fetches a list of EVM addresses.
pub fn get_accounts() -> Result<Vec<String>, Box<dyn Error>> {
    let accounts = ffi::getAccounts();
    Ok(accounts)
}

/// Retrieves the data directory path where blockchain data is stored.
/// Set with the `defid -datadir` flag.
/// Defaults are as follows:
/// - Windows < Vista: `C:\Documents and Settings\Username\Application Data\DeFi`
/// - Windows >= Vista: `C:\Users\Username\AppData\Roaming\DeFi`
/// - Mac: `~/Library/Application Support/DeFi`
/// - Unix: `~/.defi`
///
/// Note: The actual paths can be overridden by the node's `-datadir` flag.
pub fn get_datadir() -> String {
    ffi::getDatadir()
}

/// Returns the maximum number of network connections as set by node `-ethmaxconnections` flag.
/// Defaults to 100
pub fn get_max_connections() -> u32 {
    ffi::getEthMaxConnections()
}

/// Logs the auto port used by the node.
pub fn print_port_usage(port_type: u8, port_number: u16) {
    ffi::printEVMPortUsage(port_type, port_number);
}

/// Gets the maximum response size in bytes for Ethereum RPC calls.
pub fn get_max_response_byte_size() -> u32 {
    ffi::getEthMaxResponseByteSize()
}

/// Gets the maxmimum raw memory usage that a raw tracing request is allowed to use.
/// Bound the size of memory, stack and storage data.
pub fn get_tracing_raw_max_memory_usage_bytes() -> u32 {
    ffi::getEthTracingMaxMemoryUsageBytes()
}

/// Gets the suggested priority fee percentile for suggested gas price Ethereum RPC calls.
pub fn get_suggested_priority_fee_percentile() -> i64 {
    ffi::getSuggestedPriorityFeePercentile()
}

/// Gets the gas estimation error ratio for Ethereum RPC calls.
pub fn get_estimate_gas_error_ratio() -> u64 {
    ffi::getEstimateGasErrorRatio()
}

/// Retrieves the network identifier as a string.
pub fn get_network() -> String {
    ffi::getNetwork()
}

/// Returns the difficulty of a block given its hash.
pub fn get_difficulty(block_hash: [u8; 32]) -> Result<u32, Box<dyn Error>> {
    let bits = ffi::getDifficulty(block_hash);
    Ok(bits)
}

/// Retrieves the total amount of work in the blockchain up to a block, given its hash.
pub fn get_chainwork(block_hash: [u8; 32]) -> Result<[u8; 32], Box<dyn Error>> {
    let chainwork = ffi::getChainWork(block_hash);
    Ok(chainwork)
}

/// Fetches the EVM transactions from the mempool.
pub fn get_pool_transactions() -> Result<Vec<ffi::TransactionData>, Box<dyn Error>> {
    let transactions = ffi::getPoolTransactions();
    Ok(transactions)
}

/// Calculates the size of a native transaction given the raw transaction.
pub fn get_native_tx_size(data: Vec<u8>) -> Result<u64, Box<dyn Error>> {
    let tx_size = ffi::getNativeTxSize(data);
    Ok(tx_size)
}

/// Retrieves the minimum transaction fee that will be relayed by the node
pub fn get_min_relay_tx_fee() -> Result<u64, Box<dyn Error>> {
    let tx_fee = ffi::getMinRelayTxFee();
    Ok(tx_fee)
}

/// Gets the private key for the given pubkey string.
pub fn get_eth_priv_key(key: [u8; 20]) -> Result<[u8; 32], Box<dyn Error>> {
    let eth_key = ffi::getEthPrivKey(key);
    Ok(eth_key)
}

/// Returns the path to the genesis state input JSON file.
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
    let [current_block, highest_block] = ffi::getEthSyncStatus();
    Ok((current_block as i32, highest_block as i32))
}

/// Retrieves attribute values.
pub fn get_attribute_values(mnview_ptr: Option<usize>) -> ffi::Attributes {
    ffi::getAttributeValues(mnview_ptr.unwrap_or_default())
}

/// Logs a message via the cpp logger.
pub fn log_print(message: &str) {
    // TODO: Switch to u8 to avoid intermediate string conversions
    ffi::CppLogPrintf(message.to_owned());
}

/// Fetches all DST20 tokens in view, returns the result of the migration
#[allow(clippy::ptr_arg)]
pub fn get_dst20_tokens(mnview_ptr: usize, tokens: &mut Vec<ffi::DST20Token>) -> bool {
    ffi::getDST20Tokens(mnview_ptr, tokens)
}

/// Returns the number of CPU cores available to the node.
pub fn get_num_cores() -> i32 {
    ffi::getNumCores()
}

/// Retrieves the CORS allowed origin as a string for RPC calls.
pub fn get_cors_allowed_origin() -> String {
    ffi::getCORSAllowedOrigin()
}

/// Fetches the number of active network connections to the node.
pub fn get_num_connections() -> i32 {
    ffi::getNumConnections()
}

/// Gets the ECC LRU cache size.
pub fn get_ecc_lru_cache_count() -> usize {
    ffi::getEccLruCacheCount()
}

/// Gets the EVM LRU cache size.
pub fn get_evmv_lru_cache_count() -> usize {
    ffi::getEvmValidationLruCacheCount()
}

/// Gets the EVM notification channel buffer size.
pub fn get_evm_notification_channel_buffer_size() -> usize {
    ffi::getEvmNotificationChannelBufferSize()
}

/// Whether ETH-RPC debug is enabled
pub fn is_eth_debug_rpc_enabled() -> bool {
    ffi::isEthDebugRPCEnabled()
}

/// Whether debug_traceTransaction RPC is enabled
pub fn is_eth_debug_trace_rpc_enabled() -> bool {
    ffi::isEthDebugTraceRPCEnabled()
}

pub fn get_evm_system_txs_from_block(block_hash: [u8; 32]) -> Vec<ffi::SystemTxData> {
    ffi::getEVMSystemTxsFromBlock(block_hash)
}

/// Gets the DF23 height
pub fn get_df23_height() -> u64 {
    ffi::getDF23Height()
}

/// Send tokens to DVM to split
pub fn split_tokens_from_evm(
    mnview_ptr: usize,
    old_amount: ffi::TokenAmount,
    new_amount: &mut ffi::TokenAmount,
) -> bool {
    ffi::migrateTokensFromEVM(mnview_ptr, old_amount, new_amount)
}

#[cfg(test)]
mod tests {}
