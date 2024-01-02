use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct Transaction;

impl ColumnName for Transaction {
    const NAME: &'static str = "transaction";
}

impl Column for Transaction {
    type Index = String;
}

impl TypedColumn for Transaction {
    type Type = String;
}
