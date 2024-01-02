use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct OracleHistory;

impl ColumnName for OracleHistory {
    const NAME: &'static str = "oracle_history";
}

impl Column for OracleHistory {
    type Index = String;
}

impl TypedColumn for OracleHistory {
    type Type = String;
}
