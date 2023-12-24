use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct Masternode;

impl ColumnName for Masternode {
    const NAME: &'static str = "masternode";
}

impl Column for Masternode {
    type Index = Txid;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_byte_array().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Self::Index::from_slice(&raw_key)
            .map_err(|_| DBError::Custom(format_err!("Error parsing key")))
    }
}

impl TypedColumn for Masternode {
    type Type = model::Masternode;
}
