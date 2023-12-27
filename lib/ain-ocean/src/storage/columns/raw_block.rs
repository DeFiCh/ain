use ain_db::{Column, ColumnName, DBError, TypedColumn};
use bitcoin::{hashes::Hash, BlockHash};

#[derive(Debug)]
pub struct RawBlock;

impl ColumnName for RawBlock {
    const NAME: &'static str = "raw_block";
}

impl Column for RawBlock {
    type Index = BlockHash;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_byte_array().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Self::Index::from_slice(&raw_key).map_err(|_| DBError::ParseKey)
    }
}

impl TypedColumn for RawBlock {
    type Type = String;
}
