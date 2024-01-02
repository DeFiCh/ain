use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

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
