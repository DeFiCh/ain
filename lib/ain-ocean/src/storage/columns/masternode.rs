use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct Masternode;

impl ColumnName for Masternode {
    const NAME: &'static str = "masternode";
}

impl Column for Masternode {
    type Index = Txid;
}

impl TypedColumn for Masternode {
    type Type = model::Masternode;
}

// Secondary index by block height and txno
#[derive(Debug)]
pub struct MasternodeByHeight;

impl ColumnName for MasternodeByHeight {
    const NAME: &'static str = "masternode_by_height";
}

impl Column for MasternodeByHeight {
    type Index = (u32, usize);
}

impl TypedColumn for MasternodeByHeight {
    type Type = Txid;
}
