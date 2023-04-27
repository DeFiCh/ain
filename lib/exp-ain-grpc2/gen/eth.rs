#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthAccountsResult {
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
pub struct EthChainIdResult {
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
pub struct EthCallInput {
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
pub struct EthCallResult {
    /// The return value of the executed contract method
    #[prost(string, tag = "1")]
    pub data: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignInput {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub message: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignResult {
    #[prost(string, tag = "1")]
    pub signature: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBalanceInput {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBalanceResult {
    #[prost(string, tag = "1")]
    pub balance: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendTransactionInput {
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendTransactionResult {
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCoinBaseResult {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthMiningResult {
    #[prost(bool, tag = "1")]
    pub is_mining: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthHashRateResult {
    #[prost(string, tag = "1")]
    pub hash_rate: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGasPriceResult {
    #[prost(string, tag = "1")]
    pub gas_price: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthBlockNumberResult {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionCountInput {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionCountResult {
    #[prost(string, tag = "1")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByHashInput {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByHashResult {
    #[prost(string, tag = "1")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByNumberInput {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockTransactionCountByNumberResult {
    #[prost(string, tag = "1")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockHashInput {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockHashResult {
    #[prost(string, tag = "1")]
    pub number_uncles: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockNumberInput {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleCountByBlockNumberResult {
    #[prost(string, tag = "1")]
    pub number_uncles: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetCodeInput {
    #[prost(string, tag = "1")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub block_number: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetCodeResult {
    /// The code from the given address.
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignTransactionInput {
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSignTransactionResult {
    /// The signed transaction object.
    #[prost(string, tag = "1")]
    pub transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendRawTransactionInput {
    #[prost(string, tag = "1")]
    pub transaction: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSendRawTransactionResult {
    /// The transaction hash, or the zero hash if the transaction is not yet available.
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthEstimateGasInput {
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
pub struct EthEstimateGasResult {
    #[prost(string, tag = "1")]
    pub gas_used: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByHashInput {
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
    #[prost(bool, tag = "2")]
    pub full_transaction: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByHashResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByNumberInput {
    #[prost(string, tag = "1")]
    pub number: ::prost::alloc::string::String,
    #[prost(bool, tag = "2")]
    pub full_transaction: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetBlockByNumberResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByHashInput {
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByHashResult {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockHashAndIndexInput {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockHashAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockNumberAndIndexInput {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionByBlockNumberAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockHashAndIndexInput {
    #[prost(string, tag = "1")]
    pub block_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockHashAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockNumberAndIndexInput {
    #[prost(string, tag = "1")]
    pub block_number: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub index: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetUncleByBlockNumberAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetCompilersResult {
    #[prost(string, repeated, tag = "1")]
    pub compilers: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSolidityInput {
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSolidityResult {
    #[prost(string, tag = "1")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileLllInput {
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileLllResult {
    #[prost(string, tag = "1")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSerpentInput {
    #[prost(string, tag = "1")]
    pub code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthCompileSerpentResult {
    #[prost(string, tag = "1")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthProtocolVersionResult {
    #[prost(string, tag = "1")]
    pub protocol_version: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct Web3Sha3Input {
    /// The data to convert into a SHA3 hash
    #[prost(string, tag = "1")]
    pub data: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct Web3Sha3Result {
    /// The SHA3 result of the given string.
    #[prost(string, tag = "1")]
    pub data: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct NetPeerCountResult {
    #[prost(string, tag = "1")]
    pub number_peer: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct NetVersionResult {
    #[prost(string, tag = "1")]
    pub network_version: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct Web3ClientVersionResult {
    #[prost(string, tag = "1")]
    pub client_version: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetWorkResult {
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
pub struct EthSubmitWorkInput {
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
pub struct EthSubmitWorkResult {
    #[prost(bool, tag = "1")]
    pub is_valid: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSubmitHashrateInput {
    #[prost(string, tag = "1")]
    pub hash_rate: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    pub id: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthSubmitHashrateResult {
    #[prost(bool, tag = "1")]
    pub is_valid: bool,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetStorageAtInput {
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
pub struct EthGetStorageAtResult {
    /// The value at this storage position.
    #[prost(string, tag = "1")]
    pub value: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionReceiptInput {
    #[prost(string, tag = "1")]
    pub transaction_hash: ::prost::alloc::string::String,
}
#[derive(Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
pub struct EthGetTransactionReceiptResult {
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
pub struct EthSyncingResult {
    #[prost(oneof = "eth_syncing_result::StatusOrInfo", tags = "1, 2")]
    #[serde(flatten)]
    pub status_or_info: ::core::option::Option<eth_syncing_result::StatusOrInfo>,
}
/// Nested message and enum types in `EthSyncingResult`.
pub mod eth_syncing_result {
    #[derive(Eq, serde::Serialize, serde::Deserialize)]
    #[serde(rename_all = "camelCase")]
    #[allow(clippy::derive_partial_eq_without_eq)]
    #[derive(Clone, PartialEq, ::prost::Oneof)]
    pub enum StatusOrInfo {
        #[prost(bool, tag = "1")]
        Status(bool),
        #[prost(message, tag = "2")]
        SyncInfo(super::EthSyncingInfo),
    }
}
