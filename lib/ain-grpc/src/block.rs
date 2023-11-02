use std::fmt;

use ain_evm::bytes::Bytes;
use ethereum::{BlockAny, Header, TransactionV2};
use ethereum_types::{H160, H256, H64, U256};
use rlp::Encodable;
use serde::{
    de::{Error, MapAccess, Visitor},
    Deserialize, Deserializer, Serialize, Serializer,
};
use sha3::Digest;

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct RpcBlockHeader {
    // Parity specific - https://github.com/openethereum/parity-ethereum/issues/2401
    pub author: H160,
    pub base_fee_per_gas: U256,
    pub difficulty: U256,
    pub extra_data: Bytes,
    pub gas_limit: U256,
    pub gas_used: U256,
    pub hash: H256,
    pub logs_bloom: String,
    pub miner: H160,
    pub mix_hash: H256,
    pub nonce: H64,
    pub number: U256,
    pub parent_hash: H256,
    pub receipts_root: H256,
    // Parity specific - https://github.com/ethereum/EIPs/issues/95
    pub seal_fields: Vec<Bytes>,
    pub sha3_uncles: H256,
    pub size: String,
    pub state_root: H256,
    pub timestamp: U256,
    pub total_difficulty: U256,
    pub transactions_root: H256,
}

impl From<Header> for RpcBlockHeader {
    fn from(header: Header) -> Self {
        RpcBlockHeader {
            author: header.beneficiary,
            base_fee_per_gas: header.base_fee,
            difficulty: header.difficulty,
            extra_data: Bytes::from(header.extra_data.clone()),
            gas_limit: header.gas_limit,
            gas_used: header.gas_used,
            hash: header.hash(),
            logs_bloom: format!("{:#x}", header.logs_bloom),
            miner: header.beneficiary,
            mix_hash: header.mix_hash,
            nonce: header.nonce,
            number: header.number,
            parent_hash: header.parent_hash,
            receipts_root: header.receipts_root,
            seal_fields: vec![
                Bytes::from(header.mix_hash.as_fixed_bytes().to_vec()),
                Bytes::from(header.nonce.as_fixed_bytes().to_vec()),
            ],
            sha3_uncles: H256::from_slice(&sha3::Keccak256::digest(
                &rlp::RlpStream::new_list(0).out(),
            )),
            size: format!("{:#x}", header.rlp_bytes().len()),
            state_root: header.state_root,
            total_difficulty: U256::zero(),
            timestamp: header.timestamp.into(),
            transactions_root: header.transactions_root,
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct RpcBlock {
    #[serde(flatten)]
    pub header: RpcBlockHeader,
    pub transactions: BlockTransactions,
    pub uncles: Vec<H256>,
}

impl RpcBlock {
    pub fn from_block_with_tx(block: BlockAny, full_transactions: bool) -> Self {
        RpcBlock {
            header: RpcBlockHeader::from(block.header.clone()),
            transactions: if full_transactions {
                // Discard failed to retrieved transactions with flat_map.
                // Should not happen as the transaction should not make it in the block in the first place.
                BlockTransactions::Full(
                    block
                        .transactions
                        .iter()
                        .enumerate()
                        .flat_map(|(index, tx)| {
                            EthTransactionInfo::try_from_tx_block_and_index(tx, &block, index)
                        })
                        .collect(),
                )
            } else {
                BlockTransactions::Hashes(
                    block.transactions.iter().map(TransactionV2::hash).collect(),
                )
            },
            uncles: vec![],
        }
    }
}

/// Represents rpc api block number param.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, Default)]
pub enum BlockRef {
    /// Hash
    Hash {
        /// block hash
        hash: H256,
        /// only return blocks part of the canon chain
        require_canonical: bool,
    },
    /// Number
    Num(u64),
    /// Latest block
    #[default]
    Latest,
    /// Earliest block (genesis)
    Earliest,
    /// Pending block (being mined)
    Pending,
    /// The most recent crypto-economically secure block.
    /// There is no difference between Ethereum's `safe` and `finalized`
    /// in Substrate finality gadget.
    Safe,
    /// The most recent crypto-economically secure block.
    Finalized,
}

impl<'a> Deserialize<'a> for BlockRef {
    fn deserialize<D>(deserializer: D) -> Result<BlockRef, D::Error>
    where
        D: Deserializer<'a>,
    {
        deserializer.deserialize_any(BlockNumberVisitor)
    }
}

impl BlockRef {
    /// Convert block number to min block target.
    #[must_use]
    pub fn convert_to_min_block_num(&self) -> Option<u64> {
        match *self {
            BlockRef::Num(ref x) => Some(*x),
            BlockRef::Earliest => Some(0),
            _ => None,
        }
    }
}

impl Serialize for BlockRef {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            BlockRef::Hash {
                hash,
                require_canonical,
            } => serializer.serialize_str(&format!(
                "{{ 'hash': '{hash}', 'requireCanonical': '{require_canonical}'  }}"
            )),
            BlockRef::Num(ref x) => serializer.serialize_str(&format!("0x{x:x}")),
            BlockRef::Latest => serializer.serialize_str("latest"),
            BlockRef::Earliest => serializer.serialize_str("earliest"),
            BlockRef::Pending => serializer.serialize_str("pending"),
            BlockRef::Safe => serializer.serialize_str("safe"),
            BlockRef::Finalized => serializer.serialize_str("finalized"),
        }
    }
}

struct BlockNumberVisitor;

impl<'a> Visitor<'a> for BlockNumberVisitor {
    type Value = BlockRef;

    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        write!(
            formatter,
            "a block number or 'latest', 'safe', 'finalized', 'earliest' or 'pending'"
        )
    }

    fn visit_map<V>(self, mut visitor: V) -> Result<Self::Value, V::Error>
    where
        V: MapAccess<'a>,
    {
        let (mut require_canonical, mut block_number, mut block_hash) =
            (false, None::<u64>, None::<H256>);

        loop {
            let key_str: Option<String> = visitor.next_key()?;

            match key_str {
                Some(key) => match key.as_str() {
                    "blockNumber" => {
                        let value: String = visitor.next_value()?;
                        if let Some(stripped) = value.strip_prefix("0x") {
                            let number = u64::from_str_radix(stripped, 16)
                                .map_err(|e| Error::custom(format!("Invalid block number: {e}")))?;

                            block_number = Some(number);
                            break;
                        } else {
                            return Err(Error::custom(
                                "Invalid block number: missing 0x prefix".to_string(),
                            ));
                        }
                    }
                    "blockHash" => {
                        block_hash = Some(visitor.next_value()?);
                    }
                    "requireCanonical" => {
                        require_canonical = visitor.next_value()?;
                    }
                    key => return Err(Error::custom(format!("Unknown key: {key}"))),
                },
                None => break,
            };
        }

        if let Some(number) = block_number {
            return Ok(BlockRef::Num(number));
        }

        if let Some(hash) = block_hash {
            return Ok(BlockRef::Hash {
                hash,
                require_canonical,
            });
        }

        Err(Error::custom("Invalid input"))
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
    where
        E: Error,
    {
        match value {
            "latest" => Ok(BlockRef::Latest),
            "earliest" => Ok(BlockRef::Earliest),
            "pending" => Ok(BlockRef::Pending),
            "safe" => Ok(BlockRef::Safe),
            "finalized" => Ok(BlockRef::Finalized),
            _ if value.starts_with("0x") => u64::from_str_radix(&value[2..], 16)
                .map(BlockRef::Num)
                .map_err(|e| Error::custom(format!("Invalid block number: {e}"))),
            _ => value.parse::<u64>().map(BlockRef::Num).map_err(|_| {
                Error::custom("Invalid block number: non-decimal or missing 0x prefix".to_string())
            }),
        }
    }

    fn visit_string<E>(self, value: String) -> Result<Self::Value, E>
    where
        E: Error,
    {
        self.visit_str(value.as_ref())
    }

    fn visit_u64<E>(self, value: u64) -> Result<Self::Value, E>
    where
        E: Error,
    {
        Ok(BlockRef::Num(value))
    }
}

use std::str::FromStr;

use ain_evm::block::FeeHistoryData;

use crate::codegen::types::EthTransactionInfo;

impl FromStr for BlockRef {
    type Err = serde_json::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let visitor = BlockNumberVisitor;
        let result = visitor.visit_str(s).map_err(|e: serde::de::value::Error| {
            serde_json::Error::custom(format!("Error while parsing BlockNumber: {e}"))
        });
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn match_block_number(block_number: BlockRef) -> Option<u64> {
        match block_number {
            BlockRef::Num(number) => Some(number),
            BlockRef::Earliest => Some(0),
            BlockRef::Latest => Some(1000),
            BlockRef::Safe => Some(999),
            BlockRef::Finalized => Some(999),
            BlockRef::Pending => Some(1001),
            _ => None,
        }
    }

    #[test]
    fn block_number_deserialize() {
        let bn_dec: BlockRef = serde_json::from_str(r#""42""#).unwrap();
        let bn_hex: BlockRef = serde_json::from_str(r#""0x45""#).unwrap();
        let bn_u64: BlockRef = serde_json::from_str(r#"420"#).unwrap();
        let bn_tag_earliest: BlockRef = serde_json::from_str(r#""earliest""#).unwrap();
        let bn_tag_latest: BlockRef = serde_json::from_str(r#""latest""#).unwrap();
        let bn_tag_safe: BlockRef = serde_json::from_str(r#""safe""#).unwrap();
        let bn_tag_finalized: BlockRef = serde_json::from_str(r#""finalized""#).unwrap();
        let bn_tag_pending: BlockRef = serde_json::from_str(r#""pending""#).unwrap();

        assert_eq!(match_block_number(bn_dec).unwrap(), 42);
        assert_eq!(match_block_number(bn_hex).unwrap(), 69);
        assert_eq!(match_block_number(bn_u64).unwrap(), 420);
        assert_eq!(match_block_number(bn_tag_earliest).unwrap(), 0);
        assert_eq!(match_block_number(bn_tag_latest).unwrap(), 1000);
        assert_eq!(match_block_number(bn_tag_safe).unwrap(), 999);
        assert_eq!(match_block_number(bn_tag_finalized).unwrap(), 999);
        assert_eq!(match_block_number(bn_tag_pending).unwrap(), 1001);
    }
}

/// Block Transactions
#[derive(Debug, Deserialize, Clone, PartialEq)]
pub enum BlockTransactions {
    /// Only hashes
    Hashes(Vec<H256>),
    /// Full transactions
    Full(Vec<EthTransactionInfo>),
    None,
}

impl Serialize for BlockTransactions {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            BlockTransactions::Hashes(ref hashes) => hashes.serialize(serializer),
            BlockTransactions::Full(ref ts) => ts.serialize(serializer),
            BlockTransactions::None => Vec::<()>::new().serialize(serializer),
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct RpcFeeHistory {
    pub oldest_block: U256,
    pub base_fee_per_gas: Vec<U256>,
    pub gas_used_ratio: Vec<f64>,
    pub reward: Option<Vec<Vec<U256>>>,
}

impl From<FeeHistoryData> for RpcFeeHistory {
    fn from(value: FeeHistoryData) -> Self {
        Self {
            oldest_block: value.oldest_block,
            base_fee_per_gas: value.base_fee_per_gas,
            gas_used_ratio: value.gas_used_ratio,
            reward: value.reward,
        }
    }
}
