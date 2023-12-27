use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;

use crate::model;

#[derive(Debug)]
pub struct PoolSwapAggregated;

impl ColumnName for PoolSwapAggregated {
    const NAME: &'static str = "pool_swap_aggregated";
}

impl Column for PoolSwapAggregated {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl TypedColumn for PoolSwapAggregated {
    type Type = String;
}
