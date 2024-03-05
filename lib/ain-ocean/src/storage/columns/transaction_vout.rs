use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;
#[derive(Debug)]
pub struct TransactionVout;

impl ColumnName for TransactionVout {
    const NAME: &'static str = "transaction_vout";
}

impl Column for TransactionVout {
    type Index = model::TransactionVoutKey;

    fn key(index: &Self::Index) -> Result<Vec<u8>, DBError> {
        let (txid, txno) = index;
        let mut vec = txid.as_byte_array().to_vec();
        vec.extend_from_slice(&txno.to_be_bytes());
        Ok(vec)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        if raw_key.len() != 40 {
            return Err(format_err!("length of the slice is not 40").into());
        }
        let mut hash_array = [0u8; 32];
        hash_array.copy_from_slice(&raw_key[..32]);
        let mut txno_array = [0u8; 8];
        txno_array.copy_from_slice(&raw_key[32..]);

        let txid = Txid::from_byte_array(hash_array);
        let txno = usize::from_be_bytes(txno_array);
        Ok((txid, txno))
    }
}

impl TypedColumn for TransactionVout {
    type Type = model::TransactionVout;
}
