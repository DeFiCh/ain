use ain_db::{Column, ColumnName, TypedColumn};
use bitcoin::Txid;

use crate::model;

#[derive(Debug)]
pub struct TxResult;

impl ColumnName for TxResult {
    const NAME: &'static str = "tx_result";
}

impl Column for TxResult {
    type Index = Txid;
}

impl TypedColumn for TxResult {
    type Type = model::TxResult;
}
