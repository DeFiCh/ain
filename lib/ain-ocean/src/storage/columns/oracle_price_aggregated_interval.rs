use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct OraclePriceAggregatedInterval;

impl ColumnName for OraclePriceAggregatedInterval {
    const NAME: &'static str = "oracle_price_aggregated_interval";
}

impl Column for OraclePriceAggregatedInterval {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl TypedColumn for OraclePriceAggregatedInterval {
    type Type = String;
}
