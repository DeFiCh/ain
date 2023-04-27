#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthAccountsResponse {
    /// Accounts
    #[prost(string, repeated, tag = "1")]
    pub accounts: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthTransactionInfo {
    /// The address from which the transaction is sent
    #[prost(string, optional, tag = "1")]
    pub from: ::core::option::Option<::prost::alloc::string::String>,
    /// The address to which the transaction is addressed
    #[prost(string, optional, tag = "2")]
    pub to: ::core::option::Option<::prost::alloc::string::String>,
    /// The integer of gas provided for the transaction execution
    #[prost(uint64, optional, tag = "3")]
    pub gas: ::core::option::Option<u64>,
    /// The integer of gas price used for each paid gas encoded as hexadecimal
    #[prost(string, optional, tag = "4")]
    pub price: ::core::option::Option<::prost::alloc::string::String>,
    /// The integer of value sent with this transaction encoded as hexadecimal
    #[prost(string, optional, tag = "5")]
    pub value: ::core::option::Option<::prost::alloc::string::String>,
    /// The hash of the method signature and encoded parameters.
    #[prost(string, optional, tag = "6")]
    pub data: ::core::option::Option<::prost::alloc::string::String>,
    /// The integer of a nonce. This allows to overwrite your own pending transactions that use the same nonce.
    #[prost(string, optional, tag = "7")]
    pub nonce: ::core::option::Option<::prost::alloc::string::String>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthChainIdResponse {
    #[prost(string, tag = "1")]
    pub id: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthBlockInfo {
    /// The block number. null when its pending block.
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
    /// Hash of the block. null when its pending block.
    #[prost(string, tag = "2")]
    pub hash: ::prost::alloc::string::String,
    /// Hash of the parent block.
    #[prost(string, tag = "3")]
    pub parent_hash: ::prost::alloc::string::String,
    /// Hash of the generated proof-of-work. null when its pending block.
    #[prost(string, tag = "4")]
    pub nonce: ::prost::alloc::string::String,
    /// SHA3 of the uncles data in the block.
    #[prost(string, tag = "5")]
    pub sha3_uncles: ::prost::alloc::string::String,
    /// The bloom filter for the logs of the block. null when its pending block.
    #[prost(string, tag = "6")]
    pub logs_bloom: ::prost::alloc::string::String,
    /// The root of the transaction trie of the block.
    #[prost(string, tag = "7")]
    pub transactions_root: ::prost::alloc::string::String,
    /// The root of the final state trie of the block.
    #[prost(string, tag = "8")]
    pub state_root: ::prost::alloc::string::String,
    /// The root of the receipts trie of the block.
    #[prost(string, tag = "9")]
    pub receipt_root: ::prost::alloc::string::String,
    /// The address of the beneficiary to whom the mining rewards were given.
    #[prost(string, tag = "10")]
    pub miner: ::prost::alloc::string::String,
    /// Integer of the difficulty for this block.
    #[prost(string, tag = "11")]
    pub difficulty: ::prost::alloc::string::String,
    /// Integer of the total difficulty of the chain until this block.
    #[prost(string, tag = "12")]
    pub total_difficulty: ::prost::alloc::string::String,
    /// The "extra data" field of this block.
    #[prost(string, tag = "13")]
    pub extra_data: ::prost::alloc::string::String,
    /// Integer the size of this block in bytes.
    #[prost(string, tag = "14")]
    pub size: ::prost::alloc::string::String,
    /// The maximum gas allowed in this block.
    #[prost(string, tag = "15")]
    pub gas_limit: ::prost::alloc::string::String,
    /// The total used gas by all transactions in this block.
    #[prost(string, tag = "16")]
    pub gas_used: ::prost::alloc::string::String,
    /// The unix timestamp for when the block was collated.
    #[prost(string, tag = "17")]
    pub timestamps: ::prost::alloc::string::String,
    /// Array of transaction objects, or 32 Bytes transaction hashes depending on the last given parameter.
    #[prost(string, repeated, tag = "18")]
    pub transactions: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
    /// Array of uncle hashes.
    #[prost(string, repeated, tag = "19")]
    pub uncles: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthTransactionReceipt {
    /// Hash of the transaction.
    #[prost(string, tag = "1")]
    pub transaction_hash: ::prost::alloc::string::String,
    /// Integer of the transactions index position in the block.
    #[prost(string, tag = "2")]
    pub transaction_index: ::prost::alloc::string::String,
    /// Hash of the block where this transaction was in.
    #[prost(string, tag = "3")]
    pub block_hash: ::prost::alloc::string::String,
    /// Block number where this transaction was in.
    #[prost(string, tag = "4")]
    pub block_number: ::prost::alloc::string::String,
    /// Address of the sender.
    #[prost(string, tag = "5")]
    pub from: ::prost::alloc::string::String,
    /// Address of the receiver. null when its a contract creation transaction.
    #[prost(string, tag = "6")]
    pub to: ::prost::alloc::string::String,
    /// The total amount of gas used when this transaction was executed in the block.
    #[prost(string, tag = "7")]
    pub cumulative_gas_used: ::prost::alloc::string::String,
    /// The sum of the base fee and tip paid per unit of gas.
    #[prost(string, tag = "8")]
    pub effective_gas_price: ::prost::alloc::string::String,
    /// The amount of gas used by this specific transaction alone.
    #[prost(string, tag = "9")]
    pub gas_used: ::prost::alloc::string::String,
    /// The contract address created, if the transaction was a contract creation, otherwise null.
    #[prost(string, tag = "10")]
    pub contract_address: ::prost::alloc::string::String,
    /// Array of log objects, which this transaction generated.
    #[prost(string, repeated, tag = "11")]
    pub logs: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
    /// Bloom filter for light clients to quickly retrieve related logs.
    #[prost(string, tag = "12")]
    pub logs_bloom: ::prost::alloc::string::String,
    /// Integer of the transaction type, 0x00 for legacy transactions, 0x01 for access list types, 0x02 for dynamic fees. It also returns either :
    #[prost(string, tag = "13")]
    pub r#type: ::prost::alloc::string::String,
    /// 32 bytes of post-transaction stateroot (pre Byzantium)
    #[prost(string, optional, tag = "14")]
    pub root: ::core::option::Option<::prost::alloc::string::String>,
    /// Either 1 (success) or 0 (failure)
    #[prost(string, optional, tag = "15")]
    pub status: ::core::option::Option<::prost::alloc::string::String>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCallRequest {
    /// Transaction info
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
    /// Block number in hexadecimal format or the string latest, earliest, pending, safe or finalized
    #[prost(string, tag = "2")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCallResponse {
    /// The return value of the executed contract method
    #[prost(string, tag = "1")]
    pub data: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignRequest {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub message: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignResponse {
    #[prost(string, tag = "1")]
    pub signature: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBalanceRequest {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBalanceResponse {
    #[prost(string, tag = "1")]
    pub balance: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendTransactionRequest {
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendTransactionResponse {
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCoinBaseResponse {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthMiningResponse {
    #[prost(bool, tag = "1")]
    pub is_mining: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthHashRateResponse {
    #[prost(string, tag = "1")]
    pub hash_rate: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGasPriceResponse {
    #[prost(string, tag = "1")]
    pub gas_price: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthBlockNumberResponse {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionCountRequest {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionCountResponse {
    #[prost(string, tag = "1")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByHashRequest {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByHashResponse {
    #[prost(string, tag = "1")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByNumberRequest {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByNumberResponse {
    #[prost(string, tag = "1")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockHashRequest {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockHashResponse {
    #[prost(string, tag = "1")]
    pub number_uncles: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockNumberRequest {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockNumberResponse {
    #[prost(string, tag = "1")]
    pub number_uncles: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetCodeRequest {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetCodeResponse {
    /// The code from the given address.
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignTransactionRequest {
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignTransactionResponse {
    /// The signed transaction object.
    #[prost(string, tag = "1")]
    pub transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendRawTransactionRequest {
    #[prost(string, tag = "1")]
    pub transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendRawTransactionResponse {
    /// The transaction hash, or the zero hash if the transaction is not yet available.
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthEstimateGasRequest {
    /// Transaction info
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
    /// Block number in hexadecimal format or the string latest, earliest, pending, safe or finalized
    #[prost(string, optional, tag = "2")]
    pub block_number: ::core::option::Option<::prost::alloc::string::String>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthEstimateGasResponse {
    #[prost(string, tag = "1")]
    pub gas_used: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByHashRequest {
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
    #[prost(bool, tag = "2")]
    pub full_transaction: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByHashResponse {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByNumberRequest {
    #[prost(string, tag = "1")]
    pub number: ::prost::alloc::string::String,
    #[prost(bool, tag = "2")]
    pub full_transaction: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByNumberResponse {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByHashRequest {
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByHashResponse {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockHashAndIndexRequest {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockHashAndIndexResponse {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockNumberAndIndexRequest {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockNumberAndIndexResponse {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockHashAndIndexRequest {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockHashAndIndexResponse {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockNumberAndIndexRequest {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockNumberAndIndexResponse {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetCompilersResponse {
    #[prost(string, repeated, tag = "1")]
    pub compilers: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSolidityRequest {
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSolidityResponse {
    #[prost(string, tag = "1")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileLllRequest {
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileLllResponse {
    #[prost(string, tag = "1")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSerpentRequest {
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSerpentResponse {
    #[prost(string, tag = "1")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthProtocolVersionResponse {
    #[prost(string, tag = "1")]
    pub protocol_version: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct Web3Sha3Request {
    /// The data to convert into a SHA3 hash
    #[prost(string, tag = "1")]
    pub data: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct Web3Sha3Response {
    /// The SHA3 result of the given string.
    #[prost(string, tag = "1")]
    pub data: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct NetPeerCountResponse {
    #[prost(string, tag = "1")]
    pub number_peer: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct NetVersionResponse {
    #[prost(string, tag = "1")]
    pub network_version: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct Web3ClientVersionResponse {
    #[prost(string, tag = "1")]
    pub client_version: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetWorkResponse {
    /// Current block header pow-hash
    #[prost(string, tag = "1")]
    pub currentblock: ::prost::alloc::string::String,
    /// The seed hash used for the DAG.
    #[prost(string, tag = "2")]
    pub seed_hash: ::prost::alloc::string::String,
    /// The boundary condition ("target"), 2^256 / difficulty.
    #[prost(string, tag = "3")]
    pub target: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSubmitWorkRequest {
    #[prost(string, tag = "1")]
    pub nounce: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub pow_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "3")]
    pub mix_digest: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSubmitWorkResponse {
    #[prost(bool, tag = "1")]
    pub is_valid: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSubmitHashrateRequest {
    #[prost(string, tag = "1")]
    pub hash_rate: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub id: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSubmitHashrateResponse {
    #[prost(bool, tag = "1")]
    pub is_valid: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetStorageAtRequest {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub position: ::prost::alloc::string::String,
    #[prost(string, tag = "3")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetStorageAtResponse {
    /// The value at this storage position.
    #[prost(string, tag = "1")]
    pub value: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionReceiptRequest {
    #[prost(string, tag = "1")]
    pub transaction_hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionReceiptResponse {
    #[prost(message, optional, tag = "1")]
    pub transaction_receipt: ::core::option::Option<EthTransactionReceipt>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSyncingInfo {
    /// The block at which the import started (will only be reset, after the sync reached his head)
    #[prost(string, tag = "1")]
    pub starting_block: ::prost::alloc::string::String,
    /// The current block, same as eth_blockNumber
    #[prost(string, tag = "2")]
    pub current_block: ::prost::alloc::string::String,
    /// The estimated highest block
    #[prost(string, tag = "3")]
    pub highest_block: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSyncingResponse {
    #[prost(oneof = "eth_syncing_response::Value", tags = "1, 2")]
    pub value: ::core::option::Option<eth_syncing_response::Value>,
}
/// Nested message and enum types in `EthSyncingResponse`.
pub mod eth_syncing_response {
    #[derive(Eq, serde::Serialize, serde::Deserialize)]
    #[serde(rename_all = "camelCase")]
    #[serde(untagged)]
    #[allow(clippy::derive_partial_eq_without_eq)]
    #[derive(Clone, PartialEq, ::prost::Oneof)]
    pub enum Value {
        #[prost(bool, tag = "1")]
        Status(bool),
        #[prost(message, tag = "2")]
        SyncInfo(super::EthSyncingInfo),
    }
}
