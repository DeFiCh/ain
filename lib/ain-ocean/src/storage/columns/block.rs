use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, BlockHash};

use crate::model;

#[derive(Debug)]
pub struct Block;

impl ColumnName for Block {
    const NAME: &'static str = "block";
}

impl Column for Block {
    type Index = BlockHash;
}

impl TypedColumn for Block {
    type Type = model::Block;
}

// Secondary index by block height
#[derive(Debug)]
pub struct BlockByHeight;

impl ColumnName for BlockByHeight {
    const NAME: &'static str = "block_by_height";
}

impl Column for BlockByHeight {
    type Index = u32;
}

impl TypedColumn for BlockByHeight {
    type Type = BlockHash;
}
