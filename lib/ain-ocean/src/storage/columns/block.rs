use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, BlockHash};

use crate::model;

#[derive(Debug)]
pub struct Block;

impl ColumnName for Block {
    const NAME: &'static str = "block";
}

impl Column for Block {
    type Index = BlockHash;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_byte_array().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Self::Index::from_slice(&raw_key).map_err(|_| DBError::ParseKey)
    }
}

impl TypedColumn for Block {
    type Type = model::Block;
}

// Secondary index by block height
#[derive(Debug)]
pub struct BlockByHeight;

impl ColumnName for BlockByHeight {
    const NAME: &'static str = "block_by_height";
}

impl Column for BlockByHeight {
    type Index = u32;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.to_be_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        if raw_key.len() != 4 {
            return Err(DBError::Custom(format_err!("Wrong key length")));
        }

        let height_bytes = <[u8; 4]>::try_from(&raw_key[..])
            .map_err(|_| DBError::Custom(format_err!("Invalid height bytes")))?;

        Ok(u32::from_be_bytes(height_bytes))
    }
}

impl TypedColumn for BlockByHeight {
    type Type = BlockHash;
}
