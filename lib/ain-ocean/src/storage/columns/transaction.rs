use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, BlockHash, Txid};

use crate::model;
#[derive(Debug)]
pub struct Transaction;

impl ColumnName for Transaction {
    const NAME: &'static str = "transaction";
}

impl Column for Transaction {
    type Index = Txid;
}

impl TypedColumn for Transaction {
    type Type = model::Transaction;
}

pub struct TransactionByBlockHash;

impl ColumnName for TransactionByBlockHash {
    const NAME: &'static str = "transaction_by_block_hash";
}

impl Column for TransactionByBlockHash {
    type Index = model::TransactionByBlockHashKey;

    fn key(index: &Self::Index) -> Result<Vec<u8>, DBError> {
        let (hash, txno) = index;
        let mut vec = hash.as_byte_array().to_vec();
        vec.extend_from_slice(&txno.to_be_bytes());
        Ok(vec)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        if raw_key.len() != 40 {
            return Err(format_err!("Length of the slice is not 40").into());
        }
        let mut hash_array = [0u8; 32];
        hash_array.copy_from_slice(&raw_key[..32]);
        let mut txno_array = [0u8; 8];
        txno_array.copy_from_slice(&raw_key[32..]);

        let hash = BlockHash::from_byte_array(hash_array);
        let txno = usize::from_be_bytes(txno_array);
        Ok((hash, txno))
    }
}

impl TypedColumn for TransactionByBlockHash {
    type Type = Txid;
}
