use ain_db::{Column, ColumnName, TypedColumn};
use bitcoin::Txid;

use crate::model;

#[derive(Debug)]
pub struct MasternodeStats;

impl ColumnName for MasternodeStats {
    const NAME: &'static str = "masternode_stats";
}

impl Column for MasternodeStats {
    type Index = Txid;
}

impl TypedColumn for MasternodeStats {
    type Type = model::MasternodeStats;
}
