use ain_db::{Column, ColumnName, TypedColumn};
use bitcoin::{BlockHash, Txid};

use crate::model;
#[derive(Debug)]
pub struct Transaction;

impl ColumnName for Transaction {
    const NAME: &'static str = "transaction";
}

impl Column for Transaction {
    type Index = String;
}

impl TypedColumn for Transaction {
    type Type = model::Transaction;
}

pub struct TransactionByBlockHash;

impl ColumnName for TransactionByBlockHash {
    const NAME: &'static str = "transaction_by_block_hash";
}

impl Column for TransactionByBlockHash {
    type Index = BlockHash;
}

impl TypedColumn for TransactionByBlockHash {
    type Type = model::Transaction;
}
