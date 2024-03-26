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

pub struct OracleHistoryKey;

impl ColumnName for OracleHistoryKey {
    const NAME: &'static str = "oracle_history_key";
}

impl Column for OracleHistoryKey {
    type Index = Txid;
}

impl TypedColumn for OracleHistoryKey {
    type Type = model::OracleHistoryId;
}
