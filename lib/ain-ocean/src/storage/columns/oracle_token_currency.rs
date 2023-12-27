use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct OracleTokenCurrency;

impl ColumnName for OracleTokenCurrency {
    const NAME: &'static str = "oracle_token_currency";
}

impl Column for OracleTokenCurrency {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl TypedColumn for OracleTokenCurrency {
    type Type = String;
}
