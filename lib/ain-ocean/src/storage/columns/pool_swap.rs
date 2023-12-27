use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct PoolSwap;

impl ColumnName for PoolSwap {
    const NAME: &'static str = "pool_swap";
}

impl Column for PoolSwap {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl TypedColumn for PoolSwap {
    type Type = String;
}
