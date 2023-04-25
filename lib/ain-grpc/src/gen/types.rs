fn ignore_integer<T: num_traits::PrimInt + num_traits::Signed + num_traits::NumCast>(
    i: &T,
) -> bool {
    T::from(-1).unwrap() == *i
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct Transaction {
    /// Transaction hash
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
    /// Raw transaction data
    #[prost(message, optional, tag = "2")]
    pub raw: ::core::option::Option<RawTransaction>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct RawTransaction {
    /// Whether specified block is in the active chain or not (only present with explicit "blockhash" argument)
    #[prost(bool, tag = "1")]
    pub in_active_chain: bool,
    /// The serialized, hex-encoded data for 'txid'
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hex: ::prost::alloc::string::String,
    /// The transaction id (same as provided)
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub txid: ::prost::alloc::string::String,
    /// The transaction hash (differs from txid for witness transactions)
    #[prost(string, tag = "4")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
    /// The serialized transaction size
    #[prost(uint32, tag = "5")]
    pub size: u32,
    /// The virtual transaction size (differs from size for witness transactions)
    #[prost(uint32, tag = "6")]
    pub vsize: u32,
    /// The transaction's weight (between vsize*4-3 and vsize*4)
    #[prost(uint32, tag = "7")]
    pub weight: u32,
    /// The transaction version
    #[prost(uint32, tag = "8")]
    pub version: u32,
    /// The lock time
    #[prost(uint64, tag = "9")]
    pub locktime: u64,
    /// List of inputs
    #[prost(message, repeated, tag = "10")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub vin: ::prost::alloc::vec::Vec<Vin>,
    /// List of outputs
    #[prost(message, repeated, tag = "11")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub vout: ::prost::alloc::vec::Vec<Vout>,
    /// The block hash
    #[prost(string, tag = "12")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub blockhash: ::prost::alloc::string::String,
    /// The confirmations
    #[prost(string, tag = "13")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub confirmations: ::prost::alloc::string::String,
    /// The block time in seconds since UNIX epoch
    #[prost(uint64, tag = "14")]
    pub blocktime: u64,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct Vin {
    /// The transaction id
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub txid: ::prost::alloc::string::String,
    /// The output index
    #[prost(uint32, tag = "2")]
    pub vout: u32,
    /// The script signature
    #[prost(message, optional, tag = "3")]
    pub script_sig: ::core::option::Option<ScriptSig>,
    /// The script sequence number
    #[prost(uint64, tag = "4")]
    pub sequence: u64,
    /// Hex-encoded witness data
    #[prost(string, repeated, tag = "5")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub txinwitness: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
    /// DeFiChain fields
    #[prost(string, tag = "51")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub coinbase: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct ScriptSig {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    #[serde(rename = "asm")]
    pub field_asm: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hex: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct Vout {
    #[prost(double, tag = "1")]
    pub value: f64,
    #[prost(uint64, tag = "2")]
    pub n: u64,
    #[prost(message, optional, tag = "3")]
    pub script_pub_key: ::core::option::Option<PubKey>,
    #[prost(uint64, tag = "4")]
    pub token_id: u64,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct PubKey {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    #[serde(rename = "asm")]
    pub field_asm: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hex: ::prost::alloc::string::String,
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    #[serde(rename = "type")]
    pub field_type: ::prost::alloc::string::String,
    #[prost(int32, tag = "4")]
    #[serde(skip_serializing_if = "ignore_integer")]
    pub req_sigs: i32,
    #[prost(string, repeated, tag = "5")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub addresses: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct Block {
    /// Block hash (same as input, if any)
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
    /// The number of confirmations, or -1 if the block is not on the main chain
    #[prost(int64, tag = "2")]
    pub confirmations: i64,
    /// Block size
    #[prost(uint64, tag = "3")]
    pub size: u64,
    /// Block size without witness data
    #[prost(uint64, tag = "4")]
    pub strippedsize: u64,
    /// The block weight as defined in BIP 141
    #[prost(uint64, tag = "5")]
    pub weight: u64,
    /// The block height or index
    #[prost(uint64, tag = "6")]
    pub height: u64,
    /// The block version
    #[prost(uint64, tag = "7")]
    pub version: u64,
    /// The block version in hex
    #[prost(string, tag = "8")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub version_hex: ::prost::alloc::string::String,
    /// The merkle root
    #[prost(string, tag = "9")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub merkleroot: ::prost::alloc::string::String,
    /// List of transaction IDs
    #[prost(message, repeated, tag = "10")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub tx: ::prost::alloc::vec::Vec<Transaction>,
    /// The block time in seconds since UNIX epoch
    #[prost(uint64, tag = "11")]
    pub time: u64,
    /// The median block time in seconds since UNIX epoch
    #[prost(uint64, tag = "12")]
    pub mediantime: u64,
    /// The nonce used to generate the block (property exists only when PoW is used)
    #[prost(uint64, tag = "13")]
    pub nonce: u64,
    /// The bits which represent the target difficulty
    #[prost(string, tag = "14")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub bits: ::prost::alloc::string::String,
    /// The difficulty of the block
    #[prost(double, tag = "15")]
    pub difficulty: f64,
    /// Expected number of hashes required to produce the chain up to this block (in hex)
    #[prost(string, tag = "16")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub chainwork: ::prost::alloc::string::String,
    /// Number of transactions in the block
    #[prost(uint32, tag = "17")]
    pub n_tx: u32,
    /// The hash of the previous block
    #[prost(string, tag = "18")]
    #[serde(skip_serializing_if = "String::is_empty")]
    #[serde(rename = "previousblockhash")]
    pub previous_block_hash: ::prost::alloc::string::String,
    /// The hash of the next block
    #[prost(string, tag = "19")]
    #[serde(skip_serializing_if = "String::is_empty")]
    #[serde(rename = "nextblockhash")]
    pub next_block_hash: ::prost::alloc::string::String,
    /// DeFiChain fields
    #[prost(string, tag = "101")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub masternode: ::prost::alloc::string::String,
    #[prost(string, tag = "102")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub minter: ::prost::alloc::string::String,
    #[prost(uint64, tag = "103")]
    pub minted_blocks: u64,
    #[prost(string, tag = "104")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub stake_modifier: ::prost::alloc::string::String,
    #[prost(message, repeated, tag = "105")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub nonutxo: ::prost::alloc::vec::Vec<NonUtxo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct NonUtxo {
    #[prost(double, tag = "1")]
    pub anchor_reward: f64,
    #[prost(double, tag = "2")]
    pub burnt: f64,
    #[prost(double, tag = "3")]
    pub incentive_funding: f64,
    #[prost(double, tag = "4")]
    pub loan: f64,
    #[prost(double, tag = "5")]
    pub options: f64,
    #[prost(double, tag = "6")]
    pub unknown: f64,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct BlockInput {
    /// Block hash
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub blockhash: ::prost::alloc::string::String,
    /// 0 for hex-encoded data, 1 for a json object, and 2 for json object with transaction data [default: 1]
    #[prost(uint32, tag = "2")]
    pub verbosity: u32,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct BlockResult {
    /// Hex-encoded data for block hash (for verbosity 0)
    #[prost(string, tag = "1")]
    pub hash: ::prost::alloc::string::String,
    /// Block data (for verbosity 1 and 2)
    #[prost(message, optional, tag = "2")]
    pub block: ::core::option::Option<Block>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct BlockHashResult {
    /// Hex-encoded data for block hash
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthAccountsResult {
    /// Accounts
    #[prost(string, repeated, tag = "1")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub accounts: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthTransactionInfo {
    /// The address from which the transaction is sent
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub from: ::prost::alloc::string::String,
    /// The address to which the transaction is addressed
    #[prost(string, optional, tag = "2")]
    pub to: ::core::option::Option<::prost::alloc::string::String>,
    /// The integer of gas provided for the transaction execution
    #[prost(uint64, tag = "3")]
    pub gas: u64,
    /// The integer of gas price used for each paid gas encoded as hexadecimal
    #[prost(string, tag = "4")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub price: ::prost::alloc::string::String,
    /// The integer of value sent with this transaction encoded as hexadecimal
    #[prost(string, tag = "5")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub value: ::prost::alloc::string::String,
    /// The hash of the method signature and encoded parameters.
    #[prost(string, tag = "6")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub data: ::prost::alloc::string::String,
    /// The integer of a nonce. This allows to overwrite your own pending transactions that use the same nonce.
    #[prost(string, tag = "7")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub nonce: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthPendingTransactionInfo {
    /// Hash of the transaction.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
    /// The integer of a nonce. This allows to overwrite your own pending transactions that use the same nonce.
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub nonce: ::prost::alloc::string::String,
    /// Hash of the block. null when its pending block.
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_hash: ::prost::alloc::string::String,
    /// The block number. null when its pending block.
    #[prost(string, tag = "4")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
    /// Integer of the transactions index position in the block.
    #[prost(string, tag = "5")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub transaction_index: ::prost::alloc::string::String,
    /// The address from which the transaction is sent
    #[prost(string, tag = "6")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub from: ::prost::alloc::string::String,
    /// Address of the receiver. null when its a contract creation transaction.
    #[prost(string, tag = "7")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub to: ::prost::alloc::string::String,
    /// The integer of value sent with this transaction encoded as hexadecimal.
    #[prost(string, tag = "8")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub value: ::prost::alloc::string::String,
    /// The integer of gas provided for the transaction execution.
    #[prost(string, tag = "9")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub gas: ::prost::alloc::string::String,
    /// The integer of gas price used for each paid gas encoded as hexadecimal
    #[prost(string, tag = "10")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub gas_price: ::prost::alloc::string::String,
    /// The data sent along with the transaction.
    #[prost(string, tag = "11")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub input: ::prost::alloc::string::String,
    /// Signature v value
    #[prost(string, tag = "12")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub v: ::prost::alloc::string::String,
    /// Signature r value
    #[prost(string, tag = "13")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub r: ::prost::alloc::string::String,
    /// Signature s value
    #[prost(string, tag = "14")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub s: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthChainIdResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub id: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthBlockInfo {
    /// The block number. null when its pending block.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
    /// Hash of the block. null when its pending block.
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
    /// Hash of the parent block.
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub parent_hash: ::prost::alloc::string::String,
    /// Hash of the generated proof-of-work. null when its pending block.
    #[prost(string, tag = "4")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub nonce: ::prost::alloc::string::String,
    /// SHA3 of the uncles data in the block.
    #[prost(string, tag = "5")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub sha3_uncles: ::prost::alloc::string::String,
    /// The bloom filter for the logs of the block. null when its pending block.
    #[prost(string, tag = "6")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub logs_bloom: ::prost::alloc::string::String,
    /// The root of the transaction trie of the block.
    #[prost(string, tag = "7")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub transactions_root: ::prost::alloc::string::String,
    /// The root of the final state trie of the block.
    #[prost(string, tag = "8")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub state_root: ::prost::alloc::string::String,
    /// The root of the receipts trie of the block.
    #[prost(string, tag = "9")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub receipt_root: ::prost::alloc::string::String,
    /// The address of the beneficiary to whom the mining rewards were given.
    #[prost(string, tag = "10")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub miner: ::prost::alloc::string::String,
    /// Integer of the difficulty for this block.
    #[prost(string, tag = "11")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub difficulty: ::prost::alloc::string::String,
    /// Integer of the total difficulty of the chain until this block.
    #[prost(string, tag = "12")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub total_difficulty: ::prost::alloc::string::String,
    /// The "extra data" field of this block.
    #[prost(string, tag = "13")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub extra_data: ::prost::alloc::string::String,
    /// Integer the size of this block in bytes.
    #[prost(string, tag = "14")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub size: ::prost::alloc::string::String,
    /// The maximum gas allowed in this block.
    #[prost(string, tag = "15")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub gas_limit: ::prost::alloc::string::String,
    /// The total used gas by all transactions in this block.
    #[prost(string, tag = "16")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub gas_used: ::prost::alloc::string::String,
    /// The unix timestamp for when the block was collated.
    #[prost(string, tag = "17")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub timestamps: ::prost::alloc::string::String,
    /// Array of transaction objects, or 32 Bytes transaction hashes depending on the last given parameter.
    #[prost(string, repeated, tag = "18")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub transactions: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
    /// Array of uncle hashes.
    #[prost(string, repeated, tag = "19")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub uncles: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthTransactionReceipt {
    /// Hash of the transaction.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub transaction_hash: ::prost::alloc::string::String,
    /// Integer of the transactions index position in the block.
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub transaction_index: ::prost::alloc::string::String,
    /// Hash of the block where this transaction was in.
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_hash: ::prost::alloc::string::String,
    /// Block number where this transaction was in.
    #[prost(string, tag = "4")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
    /// Address of the sender.
    #[prost(string, tag = "5")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub from: ::prost::alloc::string::String,
    /// Address of the receiver. null when its a contract creation transaction.
    #[prost(string, tag = "6")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub to: ::prost::alloc::string::String,
    /// The total amount of gas used when this transaction was executed in the block.
    #[prost(string, tag = "7")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub cumulative_gas_used: ::prost::alloc::string::String,
    /// The sum of the base fee and tip paid per unit of gas.
    #[prost(string, tag = "8")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub effective_gas_price: ::prost::alloc::string::String,
    /// The amount of gas used by this specific transaction alone.
    #[prost(string, tag = "9")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub gas_used: ::prost::alloc::string::String,
    /// The contract address created, if the transaction was a contract creation, otherwise null.
    #[prost(string, tag = "10")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub contract_address: ::prost::alloc::string::String,
    /// Array of log objects, which this transaction generated.
    #[prost(string, repeated, tag = "11")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub logs: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
    /// Bloom filter for light clients to quickly retrieve related logs.
    #[prost(string, tag = "12")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub logs_bloom: ::prost::alloc::string::String,
    /// Integer of the transaction type, 0x00 for legacy transactions, 0x01 for access list types, 0x02 for dynamic fees. It also returns either :
    #[prost(string, tag = "13")]
    #[serde(skip_serializing_if = "String::is_empty")]
    #[serde(rename = "type")]
    pub field_type: ::prost::alloc::string::String,
    /// 32 bytes of post-transaction stateroot (pre Byzantium)
    #[prost(string, optional, tag = "14")]
    pub root: ::core::option::Option<::prost::alloc::string::String>,
    /// Either 1 (success) or 0 (failure)
    #[prost(string, optional, tag = "15")]
    pub status: ::core::option::Option<::prost::alloc::string::String>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCallInput {
    /// Transaction info
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
    /// Block number in hexadecimal format or the string latest, earliest, pending, safe or finalized
    #[prost(string, optional, tag = "2")]
    pub block_number: ::core::option::Option<::prost::alloc::string::String>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCallResult {
    /// The return value of the executed contract method
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub data: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSignInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub message: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSignResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub signature: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBalanceInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBalanceResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub balance: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSendTransactionInput {
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSendTransactionResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCoinBaseResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub address: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthMiningResult {
    #[prost(bool, tag = "1")]
    pub is_mining: bool,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthHashRateResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash_rate: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGasPriceResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub gas_price: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthBlockNumberResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionCountInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionCountResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockTransactionCountByHashInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_hash: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockTransactionCountByHashResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockTransactionCountByNumberInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockTransactionCountByNumberResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub number_transaction: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleCountByBlockHashInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_hash: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleCountByBlockHashResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub number_uncles: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleCountByBlockNumberInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleCountByBlockNumberResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub number_uncles: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetCodeInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetCodeResult {
    /// The code from the given address.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub code: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSignTransactionInput {
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSignTransactionResult {
    /// The signed transaction object.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub transaction: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSendRawTransactionInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub transaction: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSendRawTransactionResult {
    /// The transaction hash, or the zero hash if the transaction is not yet available.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthEstimateGasInput {
    /// Transaction info
    #[prost(message, optional, tag = "1")]
    pub transaction_info: ::core::option::Option<EthTransactionInfo>,
    /// Block number in hexadecimal format or the string latest, earliest, pending, safe or finalized
    #[prost(string, optional, tag = "2")]
    pub block_number: ::core::option::Option<::prost::alloc::string::String>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthEstimateGasResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub gas_used: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockByHashInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
    #[prost(bool, tag = "2")]
    pub full_transaction: bool,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockByHashResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct BlockNumber {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockByNumberInput {
    #[prost(message, optional, tag = "1")]
    pub block_number: ::core::option::Option<BlockNumber>,
    #[prost(bool, tag = "2")]
    pub full_transaction: bool,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetBlockByNumberResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionByHashInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionByHashResult {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionByBlockHashAndIndexInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub index: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionByBlockHashAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionByBlockNumberAndIndexInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub index: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionByBlockNumberAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub transaction: ::core::option::Option<EthTransactionInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleByBlockHashAndIndexInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub index: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleByBlockHashAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleByBlockNumberAndIndexInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub index: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetUncleByBlockNumberAndIndexResult {
    #[prost(message, optional, tag = "1")]
    pub block_info: ::core::option::Option<EthBlockInfo>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetCompilersResult {
    #[prost(string, repeated, tag = "1")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub compilers: ::prost::alloc::vec::Vec<::prost::alloc::string::String>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCompileSolidityInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub code: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCompileSolidityResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCompileLllInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub code: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCompileLllResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCompileSerpentInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub code: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthCompileSerpentResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub compiled_code: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthProtocolVersionResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub protocol_version: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct Web3Sha3Input {
    /// The data to convert into a SHA3 hash
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub data: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct Web3Sha3Result {
    /// The SHA3 result of the given string.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub data: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct NetPeerCountResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub number_peer: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct NetVersionResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub network_version: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct Web3ClientVersionResult {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub client_version: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetWorkResult {
    /// Current block header pow-hash
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub currentblock: ::prost::alloc::string::String,
    /// The seed hash used for the DAG.
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub seed_hash: ::prost::alloc::string::String,
    /// The boundary condition ("target"), 2^256 / difficulty.
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub target: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSubmitWorkInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub nounce: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub pow_hash: ::prost::alloc::string::String,
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub mix_digest: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSubmitWorkResult {
    #[prost(bool, tag = "1")]
    pub is_valid: bool,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSubmitHashrateInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub hash_rate: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub id: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSubmitHashrateResult {
    #[prost(bool, tag = "1")]
    pub is_valid: bool,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetStorageAtInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub address: ::prost::alloc::string::String,
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub position: ::prost::alloc::string::String,
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub block_number: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetStorageAtResult {
    /// The value at this storage position.
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub value: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionReceiptInput {
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub transaction_hash: ::prost::alloc::string::String,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthGetTransactionReceiptResult {
    #[prost(message, optional, tag = "1")]
    pub transaction_receipt: ::core::option::Option<EthTransactionReceipt>,
}
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSyncingInfo {
    /// The block at which the import started (will only be reset, after the sync reached his head)
    #[prost(string, tag = "1")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub starting_block: ::prost::alloc::string::String,
    /// The current block, same as eth_blockNumber
    #[prost(string, tag = "2")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub current_block: ::prost::alloc::string::String,
    /// The estimated highest block
    #[prost(string, tag = "3")]
    #[serde(skip_serializing_if = "String::is_empty")]
    pub highest_block: ::prost::alloc::string::String,
}
/// TODO make it oneof
#[allow(clippy::derive_partial_eq_without_eq)]
#[derive(Clone, PartialEq, ::prost::Message)]
#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(clippy::derive_partial_eq_without_eq)]
pub struct EthSyncingResult {
    #[prost(bool, optional, tag = "1")]
    pub status: ::core::option::Option<bool>,
    #[prost(message, optional, tag = "2")]
    pub sync_info: ::core::option::Option<EthSyncingInfo>,
}
