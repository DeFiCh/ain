use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct TransactionVin;

impl ColumnName for TransactionVin {
    const NAME: &'static str = "transaction_vin";
}

impl Column for TransactionVin {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl TypedColumn for TransactionVin {
    type Type = String;
}
