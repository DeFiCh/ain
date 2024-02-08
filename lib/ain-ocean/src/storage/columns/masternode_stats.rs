use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;

use crate::model;

#[derive(Debug)]
pub struct MasternodeStats;

impl ColumnName for MasternodeStats {
    const NAME: &'static str = "masternode_stats";
}

impl Column for MasternodeStats {
    type Index = u32;

    fn key(index: &Self::Index) -> Result<Vec<u8>, DBError> {
        Ok(index.to_be_bytes().to_vec())
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        if raw_key.len() != 4 {
            return Err(format_err!("Length of the slice is not 4").into());
        }
        let mut array = [0u8; 4];
        array.copy_from_slice(&raw_key);
        Ok(u32::from_be_bytes(array))
    }
}

impl TypedColumn for MasternodeStats {
    type Type = model::MasternodeStats;
}
