use ain_db::{Column, ColumnName, TypedColumn};
#[derive(Debug)]
pub struct TransactionVout;

impl ColumnName for TransactionVout {
    const NAME: &'static str = "transaction_vout";
}

impl Column for TransactionVout {
    type Index = String;
}

impl TypedColumn for TransactionVout {
    type Type = String;
}
