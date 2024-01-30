use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct TransactionVin;

impl ColumnName for TransactionVin {
    const NAME: &'static str = "transaction_vin";
}

impl Column for TransactionVin {
    type Index = model::TransactionVinKey;
}

impl TypedColumn for TransactionVin {
    type Type = model::TransactionVin;
}
