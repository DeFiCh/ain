use ain_db::{Column, ColumnName, TypedColumn};
use bitcoin::Txid;

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

pub struct OracleHistoryOracleIdSort;

impl ColumnName for OracleHistoryOracleIdSort {
    const NAME: &'static str = "oracle_history_oracle_id_sort";
}

impl Column for OracleHistoryOracleIdSort {
    type Index = Txid;
}

impl TypedColumn for OracleHistoryOracleIdSort {
    type Type = model::OracleHistoryId;
}
