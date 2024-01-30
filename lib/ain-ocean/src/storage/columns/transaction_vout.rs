use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;
#[derive(Debug)]
pub struct TransactionVout;

impl ColumnName for TransactionVout {
    const NAME: &'static str = "transaction_vout";
}

impl Column for TransactionVout {
    type Index = model::TransactionVoutKey;
}

impl TypedColumn for TransactionVout {
    type Type = model::TransactionVout;
}
