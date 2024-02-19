use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct OracleHistory;

impl ColumnName for OracleHistory {
    const NAME: &'static str = "oracle_history";
}

impl Column for OracleHistory {
    type Index = model::OracleHistoryId;
}

impl TypedColumn for OracleHistory {
    type Type = model::OracleHistory;
}
