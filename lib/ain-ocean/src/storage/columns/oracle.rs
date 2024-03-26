use ain_db::{Column, ColumnName, TypedColumn};
use bitcoin::Txid;

use crate::model;

#[derive(Debug)]
pub struct Oracle;

impl ColumnName for Oracle {
    const NAME: &'static str = "oracle";
}

impl Column for Oracle {
    type Index = Txid;
}

impl TypedColumn for Oracle {
    type Type = model::Oracle;
}
