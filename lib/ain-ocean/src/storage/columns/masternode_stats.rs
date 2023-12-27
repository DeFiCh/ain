use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct MasternodeStats;

impl ColumnName for MasternodeStats {
    const NAME: &'static str = "masternode_stats";
}

impl Column for MasternodeStats {
    type Index = Txid;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_byte_array().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Self::Index::from_slice(&raw_key).map_err(|_| DBError::ParseKey)
    }
}

impl TypedColumn for MasternodeStats {
    type Type = model::MasternodeStats;
}
