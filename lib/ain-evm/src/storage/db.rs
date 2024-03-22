use std::collections::HashMap;

use ain_db::{Column, ColumnName, Result, TypedColumn};
use ethereum::BlockAny;
use ethereum_types::{H160, H256, U256};

use crate::{log::LogIndex, receipt::Receipt};

pub mod columns {

    #[derive(Debug)]
    /// Column family for blocks data
    pub struct Blocks;

    #[derive(Debug)]
    /// Column family for transactions data
    pub struct Transactions;

    #[derive(Debug)]
    /// Column family for receipts data
    pub struct Receipts;

    #[derive(Debug)]
    /// Column family for block map data
    pub struct BlockMap;

    #[derive(Debug)]
    /// Column family for latest block number data
    pub struct LatestBlockNumber;

    #[derive(Debug)]
    /// Column family for address logs map data
    pub struct AddressLogsMap;

    #[derive(Debug)]
    /// Column family for code map data
    pub struct AddressCodeMap;

    #[derive(Debug)]
    /// Column family for block code map data
    pub struct BlockDeployedCodeHashes;

    #[derive(Debug)]
    /// Column family for database configuration
    pub struct Metadata;
}

//
// ColumnName impl
//
impl ColumnName for columns::Transactions {
    const NAME: &'static str = "transactions";
}

impl ColumnName for columns::Blocks {
    const NAME: &'static str = "blocks";
}

impl ColumnName for columns::Receipts {
    const NAME: &'static str = "receipts";
}

impl ColumnName for columns::BlockMap {
    const NAME: &'static str = "block_map";
}

impl ColumnName for columns::LatestBlockNumber {
    const NAME: &'static str = "latest_block_number";
}

impl ColumnName for columns::AddressLogsMap {
    const NAME: &'static str = "address_logs_map";
}

impl ColumnName for columns::AddressCodeMap {
    const NAME: &'static str = "address_code_map";
}

impl ColumnName for columns::BlockDeployedCodeHashes {
    const NAME: &'static str = "block_deployed_codes";
}

impl ColumnName for columns::Metadata {
    const NAME: &'static str = "metadata";
}

pub const COLUMN_NAMES: [&str; 9] = [
    columns::Blocks::NAME,
    columns::Transactions::NAME,
    columns::Receipts::NAME,
    columns::BlockMap::NAME,
    columns::LatestBlockNumber::NAME,
    columns::AddressLogsMap::NAME,
    columns::AddressCodeMap::NAME,
    columns::BlockDeployedCodeHashes::NAME,
    columns::Metadata::NAME,
];

//
// Column trait impl
//

impl Column for columns::Transactions {
    type Index = H256;

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        Ok(index.as_bytes().to_vec())
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        Ok(Self::Index::from_slice(&raw_key))
    }
}

impl Column for columns::Blocks {
    type Index = U256;

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        let mut bytes = [0_u8; 32];
        index.to_big_endian(&mut bytes);
        Ok(bytes.to_vec())
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        Ok(Self::Index::from(&*raw_key))
    }
}

impl Column for columns::Receipts {
    type Index = H256;

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        Ok(index.to_fixed_bytes().to_vec())
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        Ok(Self::Index::from_slice(&raw_key))
    }
}

impl Column for columns::BlockMap {
    type Index = H256;

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        Ok(index.to_fixed_bytes().to_vec())
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        Ok(Self::Index::from_slice(&raw_key))
    }
}

impl Column for columns::LatestBlockNumber {
    type Index = String;

    fn key(_index: &Self::Index) -> Result<Vec<u8>> {
        Ok(b"latest".to_vec())
    }

    fn get_key(_raw_key: Box<[u8]>) -> Result<Self::Index> {
        Ok("latest".to_string())
    }
}

impl Column for columns::AddressLogsMap {
    type Index = U256;

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        let mut bytes = [0_u8; 32];
        index.to_big_endian(&mut bytes);
        Ok(bytes.to_vec())
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        Ok(Self::Index::from(&*raw_key))
    }
}

impl Column for columns::AddressCodeMap {
    type Index = (H160, H256);

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        let mut bytes = Vec::with_capacity(20 + 32);
        bytes.extend_from_slice(&index.0.to_fixed_bytes());
        bytes.extend_from_slice(&index.1.to_fixed_bytes());
        Ok(bytes)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        let address = H160::from_slice(&raw_key[..20]);
        let code_hash = H256::from_slice(&raw_key[20..]);
        Ok((address, code_hash))
    }
}

impl Column for columns::BlockDeployedCodeHashes {
    type Index = (U256, H160);

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        let mut u256_bytes = [0_u8; 32];
        index.0.to_big_endian(&mut u256_bytes);

        let mut bytes = Vec::with_capacity(32 + 20);
        bytes.extend_from_slice(&u256_bytes);
        bytes.extend_from_slice(&index.1.to_fixed_bytes());
        Ok(bytes)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        let u256_bytes = &raw_key[0..32];
        let h160_bytes = &raw_key[32..52];

        let u256 = U256::from_big_endian(u256_bytes);
        let h160 = H160::from_slice(h160_bytes);

        Ok((u256, h160))
    }
}

impl Column for columns::Metadata {
    type Index = String;

    fn key(index: &Self::Index) -> Result<Vec<u8>> {
        Ok(index.as_bytes().to_vec())
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index> {
        Ok(String::from_utf8_lossy(&raw_key).to_string())
    }
}

//
// TypedColumn impl
//
impl TypedColumn for columns::Transactions {
    type Type = (H256, usize); // Block hash and tx index
}

impl TypedColumn for columns::Blocks {
    type Type = BlockAny;
}

impl TypedColumn for columns::Receipts {
    type Type = Receipt;
}

impl TypedColumn for columns::BlockMap {
    type Type = U256;
}

impl TypedColumn for columns::LatestBlockNumber {
    type Type = U256;
}

impl TypedColumn for columns::AddressLogsMap {
    type Type = HashMap<H160, Vec<LogIndex>>;
}

impl TypedColumn for columns::AddressCodeMap {
    type Type = Vec<u8>;
}

impl TypedColumn for columns::BlockDeployedCodeHashes {
    type Type = H256;
}
